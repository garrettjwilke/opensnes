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
 * the snes/hdma.h API, art is original (procedural, no binary assets).
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
 * - Procedural 4bpp tile generation in C (planar format), row-buffer
 *   tilemap upload — the whole example ships zero binary assets
 *
 * @par What to Observe
 * - Vertical stripes distorted into tight sine ripples flowing UPWARD
 *   at one scanline per frame (krom's exact animation rate)
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

/** @brief VRAM word address of BG1 tile graphics */
#define VRAM_BG1_GFX  0x2000
/** @brief VRAM word address of BG1 tilemap (SC_32x32) */
#define VRAM_BG1_MAP  0x6800

/**
 * @brief One sine period of BG1HOFS offsets, amplitude ±10 px.
 *
 * round(10 * sin(2*pi*i / 26)) for i in [0..25] — same amplitude and
 * (integer-rounded) period as krom's original table, giving the same
 * tight water-ripple look (~8.6 wave crests down a 224-line screen).
 * Kept as data (not computed) so the example has no dependency on the
 * math module and the table build stays trivial.
 */
static const s8 wave_sine[WAVE_PERIOD] = {
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
 * @brief Generate the two 4bpp tiles: vertical stripes, and a ruler row.
 *
 * 4bpp planar tile = 32 bytes: 8 rows of [plane0, plane1] then 8 rows of
 * [plane2, plane3]. Stripes 4px wide: pixels 0-3 color 1, 4-7 color 2 —
 * plane0 = 0xF0, plane1 = 0x0F. The ruler tile is the same with its top
 * row solid color 3 (planes 0+1 = 0xFF) for a straight horizontal
 * reference the wave must NOT bend.
 */
static void buildTiles(void) {
    u8 tiles[64];
    u8 row;
    for (row = 0; row < 8; row++) {
        tiles[row * 2]     = 0xF0;  /* tile 0 plane 0 */
        tiles[row * 2 + 1] = 0x0F;  /* tile 0 plane 1 */
        tiles[32 + row * 2]     = (row == 0) ? 0xFF : 0xF0;  /* tile 1 */
        tiles[32 + row * 2 + 1] = (row == 0) ? 0xFF : 0x0F;
    }
    /* planes 2-3 of both tiles stay 0: colors only use palette 0-3.
     * VRAM layout: tile 0 at word 16..31 relative to the gfx base is
     * planes 2/3 — dmaFillVRAM below zeroes them. */
    dmaFillVRAM(0, VRAM_BG1_GFX, 128);           /* clear tiles 0-1 fully */
    dmaCopyVram(tiles, VRAM_BG1_GFX, 16);        /* tile 0 planes 0/1 */
    dmaCopyVram(tiles + 32, VRAM_BG1_GFX + 16, 16); /* tile 1 planes 0/1 */
}

/**
 * @brief Fill the BG1 tilemap: stripes everywhere, a ruler every 8 rows.
 *
 * Built one 32-entry row at a time in a 64-byte buffer and DMA'd during
 * force blank — no 2 KB tilemap buffer needed in the C RAM band.
 */
static void buildTilemap(void) {
    u16 rowbuf[32];
    u8 x, y;
    for (y = 0; y < 32; y++) {
        u16 tile = ((y & 7) == 0) ? 1 : 0;
        for (x = 0; x < 32; x++)
            rowbuf[x] = tile;
        dmaCopyVram((u8 *)rowbuf, VRAM_BG1_MAP + y * 32, 64);
    }
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
    setMode(BG_MODE1, 0);

    /* Palette 0: dark backdrop, two stripe tones, white ruler */
    setColor(0, RGB(2, 2, 6));      /* backdrop / color 0 */
    setColor(1, RGB(10, 6, 22));    /* stripe A: deep violet */
    setColor(2, RGB(20, 12, 31));   /* stripe B: light violet */
    setColor(3, RGB(31, 31, 31));   /* ruler: white */

    buildTiles();
    buildTilemap();

    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);

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
