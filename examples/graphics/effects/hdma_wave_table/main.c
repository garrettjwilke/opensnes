/**
 * @file main.c
 * @brief Raw HDMA table in C — per-scanline wave, krom-style
 * @ingroup examples
 *
 * Builds an HDMA table BY HAND in C and animates it the way the original
 * assembler demo does: the table is written once, and each VBlank the
 * table START POINTER advances one entry — the ripple pattern flows up
 * the screen (line L reads entry phase+L, so each crest sits at
 * X0-phase) without a single byte of the table being rewritten. This is the
 * classic per-scanline effect that plain DMA cannot do: one BG1 horizontal
 * scroll value per line, traced along a sine.
 *
 * The companion example `hdma_wave` shows the same visual through the
 * library's high-level engine (hdmaWaveH / hdmaWaveUpdate, double-buffered
 * RAM tables). THIS example is the low-level counterpart: it teaches the
 * HDMA table FORMAT itself — `[line-count, value...]` entries, terminator,
 * and the repoint-per-frame animation idiom.
 *
 * C port of "SNES Wave HDMA Demo" by krom (Peter Lemon),
 * github.com/PeterLemon/SNES, PPU/HDMA/WaveHDMA — technique reproduced on
 * the snes/hdma.h API in the original demo configuration (BG Mode 3,
 * full-screen 256-color image). Art is original: procedurally generated
 * water caustics (res/water.bmp), no krom assets.
 *
 * @par SNES Concepts
 * - HDMA table format: `count` byte (1 = apply to one scanline) followed
 *   by the register payload (2 bytes for a write-twice register), then a
 *   0x00 terminator byte
 * - HDMA_MODE_1REG_2X: one register written twice per line — exactly what
 *   the 16-bit scroll registers ($210D BG1HOFS low/high) expect
 * - Animation by START-POINTER repoint (hdmaSetup once per frame with
 *   `table + phase*3`): the table itself is immutable, so HDMA never
 *   observes a partially rewritten entry — no tearing, ~zero CPU cost
 * - BG Mode 3 (8bpp, 256 colors) with a full-screen image — the same
 *   configuration as the original demo (~57 KB of unique tiles, split
 *   across two ROM banks; the tilemap sits above them at VRAM $7C00,
 *   mirroring krom's layout)
 *
 * @par What to Observe
 * - A water image distorted into tight sine ripples flowing UPWARD at
 *   one scanline per frame (krom's exact animation rate)
 * - White horizontal ruler lines every 64px stay perfectly straight
 *   (HOFS only shifts lines horizontally) while the verticals undulate
 * - No flicker or black lines: the table always covers 224 lines from
 *   any start phase
 *
 * @par Modules Used
 * console, dma, background, hdma
 *
 * @see hdma.h, examples/graphics/effects/hdma_wave (high-level engine)
 */

#include <snes.h>
#include <snes/hdma.h>

/** @brief Visible scanlines fed by the HDMA table */
#define NLINES        224
/** @brief Wave period in scanlines — krom's tight water-ripple look.
 *  (His exact samples drift slightly because his generator's period was
 *  not an integer; 26 is the closest integer period, same amplitude.) */
#define WAVE_PERIOD   26
/** @brief Bytes per HDMA entry in 1REG_2X mode: count + 16-bit value */
#define ENTRY_BYTES   3

/** @brief VRAM word address of BG1 tile graphics (image start) */
#define VRAM_BG1_GFX  0x0000
/** @brief VRAM word address of the second tile half (bytes 32768+) */
#define VRAM_BG1_GFX2 0x4000
/** @brief VRAM word address of BG1 tilemap — above the 57 KB of tiles,
 *  krom's layout (his BG1SC put the map at VRAM byte $F800) */
#define VRAM_BG1_MAP  0x7C00

/** @brief Mode 3 image data (from data.asm; generated original art) */
extern u8 tiles[], tiles_end[];
extern u8 tiles2[], tiles2_end[];
extern u8 tilemap[], tilemap_end[];
extern u8 palette[], palette_end[];

/**
 * @brief One sine period of BG1HOFS offsets, amplitude ±10 px.
 *
 * round(10 * sin(2*pi*i / 26)) for i in [0..25] — same amplitude and
 * (integer-rounded) period as krom's original table, giving the same
 * tight water-ripple look (~8.6 wave crests down a 224-line screen).
 * Kept as data (not computed) so the example has no dependency on the
 * math module and the table build stays trivial. RAM, not const: the
 * 57 KB image fills bank $00's free ROM, so a C-deref'd const here
 * spills to bank $01+ and reads as garbage (the bank $00 ratchet
 * hard-fails the build — this is the documented RAM mitigation,
 * 26 bytes copied by data_init).
 */
static s8 wave_sine[WAVE_PERIOD] = {
      0,   2,   5,   7,   8,   9,  10,  10,   9,   8,   7,   5,   2,
      0,  -2,  -5,  -7,  -8,  -9, -10, -10,  -9,  -8,  -7,  -5,  -2,
};

/**
 * @brief The HDMA table, built once at init and never rewritten.
 *
 * (NLINES + WAVE_PERIOD) entries so that from ANY start phase in
 * [0, WAVE_PERIOD) there are always >= NLINES valid entries ahead —
 * krom's trick (his table is 224+672 entries: his generator's period
 * was non-integer, so his seamless wrap needed 672 lines; our integer
 * 26-line period wraps in 26).
 * The trailing 0x00 terminator is belt-and-braces: with the invariant
 * above HDMA never reaches it, but it makes constant tweaks safe.
 */
static u8 wave_table[(NLINES + WAVE_PERIOD) * ENTRY_BYTES + 1];

/** @brief Current wave phase in scanlines (start entry of the table) */
static u16 wave_phase;

/**
 * @brief Build the immutable HDMA table: one `{1, sin, sin>>8}` per line.
 *
 * Each entry means "for 1 scanline, write these 2 bytes to BG1HOFS".
 * The 16-bit value is the sign-extended sine offset — negative offsets
 * scroll the line left, positive right.
 */
static void buildWaveTable(void) {
    u16 i;
    u8 *p = wave_table;
    for (i = 0; i < NLINES + WAVE_PERIOD; i++) {
        s16 off = wave_sine[i % WAVE_PERIOD];
        *p++ = 1;                        /* count: apply to 1 scanline */
        *p++ = (u8)(off & 0xFF);         /* BG1HOFS low byte */
        *p++ = (u8)((off >> 8) & 0xFF);  /* BG1HOFS high byte (sign) */
    }
    *p = 0;                              /* HDMA terminator */
}

/**
 * @brief Entry point — raw HDMA wave demo.
 *
 * Init order per the SDK convention: console, mode, palette, tiles,
 * tilemap, BG pointers, HDMA setup, screen on. The main loop is the
 * krom idiom: one hdmaSetup() repoint per VBlank, nothing else.
 *
 * @return Never returns (infinite loop)
 */
int main(void) {
    consoleInit();

    /* Load the full-screen 8bpp image: two tile halves (>32KB tileset),
     * tilemap above them, 256-color palette — krom's Mode 3 setup on
     * the SDK API. All transfers run during the boot force blank. */
    dmaCopyVram(tiles,   VRAM_BG1_GFX,  (u16)(tiles_end - tiles));
    dmaCopyVram(tiles2,  VRAM_BG1_GFX2, (u16)(tiles2_end - tiles2));
    dmaCopyVram(tilemap, VRAM_BG1_MAP,  (u16)(tilemap_end - tilemap));
    dmaCopyCGram(palette, 0, (u16)(palette_end - palette));

    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    setMode(BG_MODE3, 0);

    /* The table is built ONCE; animation only moves the start pointer. */
    buildWaveTable();
    wave_phase = 0;
    hdmaSetup(HDMA_CHANNEL_0, HDMA_MODE_1REG_2X, HDMA_DEST_BG1HOFS,
              wave_table);
    hdmaEnable(1 << HDMA_CHANNEL_0);

    setMainScreen(TM_BG1);
    setScreenOn();

    while (1) {
        WaitForVBlank();
        /* Advance one entry per frame, wrapping on the sine period —
         * the wave scrolls downward. Repointing during VBlank is safe:
         * HDMA reloads its table address at the start of every frame. */
        wave_phase++;
        if (wave_phase >= WAVE_PERIOD)
            wave_phase = 0;
        hdmaSetup(HDMA_CHANNEL_0, HDMA_MODE_1REG_2X, HDMA_DEST_BG1HOFS,
                  wave_table + wave_phase * ENTRY_BYTES);
    }

    return 0;
}
