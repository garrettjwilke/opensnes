/*
 * benchrom — the C1 audit's measuring instrument.
 *
 * For each measured function: run it in a tight N-iteration loop and
 * record how many NMI frames elapsed (frame_count is incremented by
 * the NMI handler regardless of WaitForVBlank). The python runner
 * (bench.py) converts frames -> ~CPU cycles/call after subtracting
 * the calibration loop (same loop shape, empty body), so loop
 * overhead and the NMI handler's own per-frame cost cancel out.
 *
 * Relative precision is the point: the SAME harness measures the ASM
 * original and its C port — the +-10 % migration rule of the C1 audit
 * (.claude/notes/chantiers/c1_asm_audit.md) compares two numbers
 * produced by identical machinery.
 *
 * Results are WRAM globals read by symbol name (luna state --peek),
 * libtest-style. r_bench_done = 0xBEEF marks completion.
 */

#include <snes.h>
#include <snes/mode7.h>
#include <snes/map.h>

/* Iterations per measured function. Chosen so cheap fns still span
 * >= ~20 frames (quantization < 5 %). volatile so the loop counter
 * compare can't be folded. */
#define N_ITER 20000u

/* --- results: frames elapsed per N_ITER-loop --- */
u16 r_cal_empty;     /* calibration: empty loop                    */
u16 r_m7_setangle;   /* mode7SetAngle(a++)                         */
u16 r_m7_setscale;   /* mode7SetScale(s, s)                        */
u16 r_m7_setcenter;  /* mode7SetCenter(x, y)                       */
u16 r_m7_setmatrix;  /* mode7SetMatrix(a, b, c, d)                 */
u16 r_m7_transform;  /* mode7Transform(deg, scale) — the heavy one */

/* map.asm measurement points (real mapscroll data, loaded once).
 * mapVblank is called OUTSIDE VBlank on purpose: the PPU ignores the
 * VRAM writes but the CPU work — the thing we measure — is identical. */
u16 r_map_getmeta;   /* mapGetMetaTile(1280, 80)                    */
u16 r_map_getprop;   /* mapGetMetaTilesProp(1280, 80)               */
u16 r_map_camera;    /* mapUpdateCamera(sweep x, 0)                 */
u16 r_map_update;    /* mapUpdate() after a camera move             */
u16 r_map_vblankf;   /* mapVblank() with pending scroll work        */

/* C-port probe for the map migration assessment: a C mapGetMetaTile
 * modelling the full-port architecture — small scalars in C statics
 * (bank 0), bulk state (row LUT at $7E, map data in its ROM bank)
 * through cached far pointers. Correctness pinned against the ASM
 * (same query must yield tile 21, as in libtest). */
u16 r_map_getmeta_c; /* the C model's cycles                        */
u16 r_map_c_val;     /* c_getmetatile(1280,80) -> 21 (correctness)  */
u16 r_bench_done;    /* 0xBEEF when every result above is written  */

static volatile u16 vi;   /* opaque loop bound (defeats folding) */

/* --- C-port probe: far-access mapGetMetaTile model --- */
extern u16 mapadrrowlut[];        /* $7E RAMSECTION (map.asm) */
extern u8 mapdata[], tilesetdef[], tilesetatt[];
static const u16 *lutp;           /* far pointer, cached once  */
static const u8 *mapp;

static u16 c_getmetatile(u16 x, u16 y) {
    u16 off = lutp[((y >> 2) & 0xFFFE) >> 1] + ((x >> 2) & 0xFFFE);
    return *(const u16 *)(mapp + off) & 0x03FF;
}

/* Frame bracket helpers */
static u16 t0;
static void bench_begin(void) {
    u16 f = frame_count;
    while (frame_count == f) { }      /* align to a frame edge */
    t0 = frame_count;
}
static u16 bench_end(void) {
    return (u16)(frame_count - t0);
}

int main(void) {
    u16 i;
    u8 a = 0;
    s16 x = 12;

    consoleInit();
    mode7Init();
    setScreenOn();

    vi = N_ITER;

    /* calibration — identical loop shape, empty body */
    bench_begin();
    for (i = 0; i < vi; i++) { }
    r_cal_empty = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mode7SetAngle(a);
        a++;
    }
    r_m7_setangle = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mode7SetScale(0x0100, 0x0100);
    }
    r_m7_setscale = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mode7SetCenter(x, (s16)(x + 3));
    }
    r_m7_setcenter = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mode7SetMatrix(256, 0, 0, 256);
    }
    r_m7_setmatrix = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mode7Transform(i & 511, 100);
    }
    r_m7_transform = bench_end();

    /* --- map module (real data) --- */
    mapLoad(mapdata, tilesetdef, tilesetatt);
    lutp = mapadrrowlut;          /* far pointers for the C model */
    mapp = mapdata + 8;           /* +8: past the m16 header, as mapLoad */

    bench_begin();
    for (i = 0; i < vi; i++) {
        mapGetMetaTile(1280, 80);
    }
    r_map_getmeta = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mapGetMetaTilesProp(1280, 80);
    }
    r_map_getprop = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mapUpdateCamera(i & 511, 0);   /* pans back and forth: streams */
    }
    r_map_camera = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mapUpdateCamera(i & 511, 0);
        mapUpdate();
    }
    r_map_update = bench_end();

    bench_begin();
    for (i = 0; i < vi; i++) {
        mapUpdateCamera(i & 511, 0);
        mapUpdate();
        mapVblank();
    }
    r_map_vblankf = bench_end();

    r_map_c_val = c_getmetatile(1280, 80);
    bench_begin();
    for (i = 0; i < vi; i++) {
        c_getmetatile(1280, 80);
    }
    r_map_getmeta_c = bench_end();

    r_bench_done = 0xBEEF;

    while (1) {
        WaitForVBlank();
    }
    return 0;
}
