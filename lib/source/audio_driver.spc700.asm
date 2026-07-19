;==============================================================================
; OpenSNES Audio System v2 — resident SPC700 driver
;==============================================================================
; Command-driven sample-playback engine: the 65816 C layer (audio.c)
; sends one command at a time over the APU I/O ports; this loop decodes
; it, pokes the S-DSP, and acks. No sequencer, no mixing — the DSP's 8
; hardware voices ARE the engine.
;
; Protocol (see .claude/notes/chantiers/audio_v2.md):
;   $F4 in  = command byte: (seq << 6) | opcode, seq cycles 1..3 so two
;             consecutive commands always differ; 0 = idle; $FE is
;             reserved for APU_CHECK_RESET (apuReset() hot-swap works
;             over this driver).
;   $F5 in  = param0 (8-bit)   $F6/$F7 in  = param1 (16-bit LE)
;   $F4 out = ack (echo of the command byte, written AFTER execution)
;   $F5 out = result0          $F6/$F7 out = result1 (16-bit LE)
;
; ARAM map: driver $0200-$09FF (2 KB budget, enforced by lib/Makefile),
; sample directory $0A00 (64 x 4 bytes), samples $0B00 and up.
;
; Register discipline: the main loop owns A/X/Y freely; no interrupts,
; no timers — a pure reactive poll (~30 cycles/iteration idle).
;==============================================================================

.include "memmap_spc700.inc"

.MACRO WDSP ARGS reg, val
    mov $F2, #reg
    mov $F3, #val
.ENDM

; Zero-page driver state
.DEFINE ZP_LAST_CMD $00     ; last executed command byte (dedup)
.DEFINE ZP_CMD      $01     ; current command byte (for the ack)
.DEFINE ZP_P0       $02     ; latched param0
.DEFINE ZP_P1L      $03     ; latched param1 low
.DEFINE ZP_P1H      $04     ; latched param1 high

.DEFINE DRIVER_VERSION 1

.BANK 0 SLOT 0
.ORG $0200

start:
    ; --- one-time DSP safe state ---------------------------------------
    WDSP $6C, $20       ; FLG: echo writes off, mute off, noise clock 0
    WDSP $5C, $FF       ; KOFF: release everything a previous program left
    WDSP $4C, $00       ; KON clear
    WDSP $2D, $00       ; PMON off
    WDSP $3D, $00       ; NON off
    WDSP $4D, $00       ; EON off
    WDSP $2C, $00       ; EVOLL 0
    WDSP $3C, $00       ; EVOLR 0
    WDSP $0C, 127       ; MVOL: header default (audioSetVolume can change)
    WDSP $1C, 127
    WDSP $5D, $0A       ; DIR: sample directory page $0A00
    WDSP $5C, $00       ; end the blanket KOFF

    mov a, #$00
    mov ZP_LAST_CMD, a
    mov $F4, a          ; ack port = 0: "no command executed yet"

;------------------------------------------------------------------------------
; Main command loop
;------------------------------------------------------------------------------
main_loop:
    APU_CHECK_RESET     ; $FE => silence DSP, ack, back to IPL ($FFC0)

    mov a, $F4          ; command byte from the CPU
    beq main_loop       ; 0 = idle
    cmp a, ZP_LAST_CMD
    beq main_loop       ; input latch still holds the handled command

    ; latch command + params (params were written BEFORE the command
    ; byte on the CPU side, so they are stable now)
    mov ZP_CMD, a
    mov ZP_LAST_CMD, a
    mov a, $F5
    mov ZP_P0, a
    mov a, $F6
    mov ZP_P1L, a
    mov a, $F7
    mov ZP_P1H, a

    ; opcode = cmd & $3F, dispatched through a jump table (a beq chain
    ; would exceed the +-128 branch range past a few handlers)
    mov a, ZP_CMD
    and a, #$3F
    cmp a, #$0F
    bcs cmd_unknown     ; opcode >= $0F: not in the table
    asl a
    mov x, a
    jmp [!cmd_table+x]

cmd_unknown:
    ; ack anyway so the CPU never times out on a version skew —
    ; result0 = $FF flags the unknown opcode
    mov a, #$FF
    mov $F5, a
    jmp !ack

cmd_table:
    .dw cmd_unknown     ; $00 (idle byte never dispatches, but (seq<<6)|0 could)
    .dw cmd_mvol        ; $01
    .dw cmd_kon         ; $02
    .dw cmd_koff        ; $03
    .dw cmd_vvol        ; $04
    .dw cmd_vpitch      ; $05
    .dw cmd_vadsr       ; $06
    .dw cmd_vgain       ; $07
    .dw cmd_unknown     ; $08 (phase 3: ECHO_CFG)
    .dw cmd_unknown     ; $09 (phase 3: ECHO_FIR)
    .dw cmd_unknown     ; $0A (phase 3: ECHO_ON)
    .dw cmd_unknown     ; $0B (phase 2: DIR_SET)
    .dw cmd_unknown     ; $0C (phase 2: LOAD)
    .dw cmd_unknown     ; $0D (phase 3: ENVX)
    .dw cmd_ping        ; $0E

;------------------------------------------------------------------------------
; $01 MVOL — p0 = master volume, both channels
;------------------------------------------------------------------------------
cmd_mvol:
    mov $F2, #$0C       ; MVOLL
    mov a, ZP_P0
    mov $F3, a
    mov $F2, #$1C       ; MVOLR
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $02 KON — p0 = voice (0-7), p1lo = directory index.
; Sets VxSRCN then keys the voice on. Volume/pitch/envelope are
; separate commands (the C layer sends them first).
;------------------------------------------------------------------------------
cmd_kon:
    mov a, ZP_P0
    xcn a               ; voice << 4 (voice <= 7 so the high nibble is 0)
    or a, #$04          ; VxSRCN
    mov $F2, a
    mov a, ZP_P1L
    mov $F3, a
    mov x, ZP_P0
    mov a, !bit_mask+x
    mov $F2, #$4C       ; KON
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $03 KOFF — p0 = voice MASK (CPU computes 1<<voice, $FF = all).
; Pulse: set the mask, let the DSP latch it, clear it again.
;------------------------------------------------------------------------------
cmd_koff:
    mov $F2, #$5C       ; KOFF
    mov a, ZP_P0
    mov $F3, a
    mov y, #96          ; ~2 DSP sample periods so the release latches
-   dbnz y, -
    mov a, #$00
    mov $F3, a          ; KOFF clear (F2 still $5C)
    jmp !ack

;------------------------------------------------------------------------------
; $04 VVOL — p0 = voice, p1lo = volL, p1hi = volR
;------------------------------------------------------------------------------
cmd_vvol:
    mov a, ZP_P0
    xcn a
    mov $F2, a          ; VxVOLL (base $00)
    mov a, ZP_P1L
    mov $F3, a
    mov a, ZP_P0
    xcn a
    or a, #$01          ; VxVOLR
    mov $F2, a
    mov a, ZP_P1H
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $05 VPITCH — p0 = voice, p1 = 14-bit DSP pitch ($1000 = 1.0x)
;------------------------------------------------------------------------------
cmd_vpitch:
    mov a, ZP_P0
    xcn a
    or a, #$02          ; VxPITCHL
    mov $F2, a
    mov a, ZP_P1L
    mov $F3, a
    mov a, ZP_P0
    xcn a
    or a, #$03          ; VxPITCHH
    mov $F2, a
    mov a, ZP_P1H
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $06 VADSR — p0 = voice, p1lo = ADSR1, p1hi = ADSR2 (packed by C)
;------------------------------------------------------------------------------
cmd_vadsr:
    mov a, ZP_P0
    xcn a
    or a, #$05          ; VxADSR1
    mov $F2, a
    mov a, ZP_P1L
    mov $F3, a
    mov a, ZP_P0
    xcn a
    or a, #$06          ; VxADSR2
    mov $F2, a
    mov a, ZP_P1H
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $07 VGAIN — p0 = voice, p1lo = raw GAIN byte (also clears ADSR1 so
; GAIN mode actually takes effect — hot-swap hygiene lesson from
; apu_switch: ADSR1 bit 7 silently overrides GAIN)
;------------------------------------------------------------------------------
cmd_vgain:
    mov a, ZP_P0
    xcn a
    or a, #$05          ; VxADSR1 = 0 -> GAIN mode
    mov $F2, a
    mov a, #$00
    mov $F3, a
    mov a, ZP_P0
    xcn a
    or a, #$07          ; VxGAIN
    mov $F2, a
    mov a, ZP_P1L
    mov $F3, a
    jmp !ack

;------------------------------------------------------------------------------
; $0E PING — result0 = driver version (audioIsReady / init handshake)
;------------------------------------------------------------------------------
cmd_ping:
    mov a, #DRIVER_VERSION
    mov $F5, a
    jmp !ack

;------------------------------------------------------------------------------
; Common ack: echo the full command byte on $F4 out
;------------------------------------------------------------------------------
ack:
    mov a, ZP_CMD
    mov $F4, a
    jmp !main_loop

bit_mask:
    .db $01, $02, $04, $08, $10, $20, $40, $80
