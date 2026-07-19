/**
 * @file audio.c
 * @brief OpenSNES Audio System v2 — C command layer over the resident
 *        SPC700 driver (audio_driver.spc700.asm)
 *
 * Replaces the legacy PVSnesLib-ABI audio.asm (retired 2026-07 — it
 * had zero callers and its multi-arg functions read garbage under
 * cc65816; see .claude/notes/tech/audio_legacy_pvsneslib_abi.md).
 * Pure C: the cc65816 ABI comes for free and the ABI lint has nothing
 * to verify. None of these calls sit on a per-frame hot path.
 *
 * Protocol (spec: .claude/notes/chantiers/audio_v2.md): one command at
 * a time over $2140-$2143. Command byte = (seq << 6) | opcode with seq
 * cycling 1..3, so consecutive commands always differ and the driver's
 * echo-ack is unambiguous. $FE is never sent: it is APU_RESET_MAGIC,
 * so apuReset() hot-swap works over the driver. Every wait is bounded
 * — the audio API returns AUDIO_ERR_TIMEOUT instead of hanging.
 *
 * Main-thread only: do not call audio functions from an NMI callback
 * (same stance as the DMA queue).
 */

#include <snes/audio.h>
#include <snes/apu.h>

/* APU I/O ports. Reading returns the SPC700's output latch; writing
 * sets its input latch — same address, different registers. volatile
 * is honoured by QBE (chantier A2). */
#define APU_IO0 (*(volatile u8 *)0x2140)
#define APU_IO1 (*(volatile u8 *)0x2141)
#define APU_IO2 (*(volatile u8 *)0x2142)
#define APU_IO3 (*(volatile u8 *)0x2143)

/* Driver opcodes (must match audio_driver.spc700.asm's cmd_table) */
#define OP_MVOL    0x01
#define OP_KON     0x02
#define OP_KOFF    0x03
#define OP_VVOL    0x04
#define OP_VPITCH  0x05
#define OP_VADSR   0x06
#define OP_VGAIN   0x07
#define OP_PING    0x0E

#define DRIVER_VERSION 1
#define SPC_DRIVER_BASE 0x0200

/* Bounded-wait iterations for one ack (~2 frames of slow-ROM loop). */
#define ACK_SPIN_MAX 20000

/*============================================================================
 * Module state (WRAM mirror — the driver is write-mostly; reads that
 * don't need the DSP are answered from here without an APU round-trip)
 *============================================================================*/

extern u8 audio_driver_blob[], audio_driver_blob_end[];

static u8 audio_ready;          /* PING succeeded after upload */
static u8 audio_seq;            /* command sequence bits, cycles 1..3 */
static u8 audio_mvol;           /* mirrored master volume */

typedef struct {
    u8 sample_id;
    u8 volume;
    u8 pan;
    u16 pitch;
} VoiceMirror;

static VoiceMirror voice_mirror[AUDIO_MAX_VOICES];

/*============================================================================
 * Command primitive
 *============================================================================*/

/**
 * @brief Send one command to the driver and wait (bounded) for the ack.
 * @return AUDIO_OK or AUDIO_ERR_TIMEOUT
 */
static u8 cmd_send(u8 op, u8 p0, u16 p1) {
    u8 cmd;
    u16 spin;

    audio_seq = (u8)(audio_seq >= 3 ? 1 : audio_seq + 1);
    cmd = (u8)((audio_seq << 6) | op);

    APU_IO1 = p0;
    APU_IO2 = (u8)p1;
    APU_IO3 = (u8)(p1 >> 8);
    APU_IO0 = cmd;

    for (spin = 0; spin < ACK_SPIN_MAX; spin++) {
        if (APU_IO0 == cmd) {
            return AUDIO_OK;
        }
    }
    return AUDIO_ERR_TIMEOUT;
}

/*============================================================================
 * Initialization
 *============================================================================*/

void audioInit(void) {
    u8 i;

    audio_ready = 0;
    audio_seq = 0;
    audio_mvol = AUDIO_VOL_MAX;
    for (i = 0; i < AUDIO_MAX_VOICES; i++) {
        voice_mirror[i].sample_id = 0xFF;
        voice_mirror[i].volume = AUDIO_VOL_MAX;
        voice_mirror[i].pan = AUDIO_PAN_CENTER;
        voice_mirror[i].pitch = AUDIO_PITCH_DEFAULT;
    }

    apuWaitBoot();
    apuUpload(audio_driver_blob, SPC_DRIVER_BASE,
              (u16)(audio_driver_blob_end - audio_driver_blob));
    apuExecute(SPC_DRIVER_BASE);

    /* Handshake: the driver answers PING with its version on result0 */
    if (cmd_send(OP_PING, 0, 0) == AUDIO_OK && APU_IO1 == DRIVER_VERSION) {
        audio_ready = 1;
    }
}

u8 audioIsReady(void) {
    return audio_ready;
}

void audioUpdate(void) {
    /* v2 is command-driven — nothing to pump. Kept as a no-op for
     * source compatibility with the historical API. */
}

/*============================================================================
 * Master volume
 *============================================================================*/

void audioSetVolume(u8 volume) {
    if (volume > AUDIO_VOL_MAX) {
        volume = AUDIO_VOL_MAX;
    }
    if (cmd_send(OP_MVOL, volume, 0) == AUDIO_OK) {
        audio_mvol = volume;
    }
}

u8 audioGetVolume(void) {
    return audio_mvol;
}

/*============================================================================
 * Voice control
 *============================================================================*/

void audioStopVoice(u8 voice) {
    if (voice >= AUDIO_MAX_VOICES) {
        return;
    }
    cmd_send(OP_KOFF, (u8)(1 << voice), 0);
}

void audioStopAll(void) {
    cmd_send(OP_KOFF, 0xFF, 0);
}

void audioSetVoiceVolume(u8 voice, u8 volumeL, u8 volumeR) {
    if (voice >= AUDIO_MAX_VOICES) {
        return;
    }
    if (cmd_send(OP_VVOL, voice,
                 (u16)((u16)volumeR << 8 | volumeL)) == AUDIO_OK) {
        /* mirror keeps the max of both for state reporting */
        voice_mirror[voice].volume = volumeL > volumeR ? volumeL : volumeR;
    }
}

void audioSetVoicePitch(u8 voice, u16 pitch) {
    if (voice >= AUDIO_MAX_VOICES) {
        return;
    }
    if (pitch > 0x3FFF) {
        pitch = 0x3FFF;        /* DSP pitch is 14-bit */
    }
    if (cmd_send(OP_VPITCH, voice, pitch) == AUDIO_OK) {
        voice_mirror[voice].pitch = pitch;
    }
}

void audioSetADSR(u8 voice, u8 attack, u8 decay, u8 sustain, u8 release) {
    u8 adsr1, adsr2;
    if (voice >= AUDIO_MAX_VOICES) {
        return;
    }
    adsr1 = (u8)(0x80 | ((decay & 0x07) << 4) | (attack & 0x0F));
    adsr2 = (u8)(((sustain & 0x07) << 5) | (release & 0x1F));
    cmd_send(OP_VADSR, voice, (u16)((u16)adsr2 << 8 | adsr1));
}

void audioSetGain(u8 voice, u8 mode) {
    if (voice >= AUDIO_MAX_VOICES) {
        return;
    }
    /* driver also zeroes VxADSR1 so GAIN mode actually applies */
    cmd_send(OP_VGAIN, voice, mode);
}

/*============================================================================
 * Sample management — PHASE 2 (not implemented yet)
 *
 * The stubs below return honest errors so callers can already code
 * against the final signatures. LOAD/DIR_SET streaming lands next.
 *============================================================================*/

u8 audioLoadSample(u8 id, const u8 *brrData, u16 size, u16 loopPoint) {
    (void)id; (void)brrData; (void)size; (void)loopPoint;
    return AUDIO_ERR_NOT_LOADED;    /* phase 2 */
}

void audioUnloadSample(u8 id) {
    (void)id;                       /* phase 2 */
}

u8 audioGetSampleInfo(u8 id, AudioSample *info) {
    (void)id; (void)info;
    return AUDIO_ERR_NOT_LOADED;    /* phase 2 */
}

u16 audioGetFreeMemory(void) {
    return 0;                       /* phase 2 */
}

u8 audioPlaySample(u8 sampleId) {
    (void)sampleId;
    return 0xFF;                    /* phase 2 (needs loaded samples) */
}

u8 audioPlaySampleEx(u8 sampleId, u8 volume, u8 pan, u16 pitch) {
    (void)sampleId; (void)volume; (void)pan; (void)pitch;
    return 0xFF;                    /* phase 2 */
}

void audioGetVoiceState(u8 voice, AudioVoiceState *state) {
    if (voice >= AUDIO_MAX_VOICES || !state) {
        return;
    }
    state->active = 0;              /* phase 3: ENVX poll */
    state->sampleId = voice_mirror[voice].sample_id;
    state->volume = voice_mirror[voice].volume;
    state->pan = voice_mirror[voice].pan;
    state->pitch = voice_mirror[voice].pitch;
}

/*============================================================================
 * Echo — PHASE 3 (not implemented yet)
 *============================================================================*/

void audioSetEcho(u8 delay, s8 feedback, s8 volumeL, s8 volumeR) {
    (void)delay; (void)feedback; (void)volumeL; (void)volumeR;
}

void audioSetEchoFilter(const s8 fir[8]) {
    (void)fir;
}

void audioEnableEcho(u8 voiceMask) {
    (void)voiceMask;
}

void audioDisableEcho(void) {
}
