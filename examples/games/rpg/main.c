/**
 * @file main.c
 * @brief RPG template (#7) — Tiled-driven overworld with dialog boxes
 * @ingroup examples
 *
 * The SDK's modules composed into a playable RPG skeleton. Two things
 * make it a template rather than a demo:
 *
 * 1. **The map is a real Tiled map** (`res/town.tmj`): terrain,
 *    per-tile collision (the `attribute` property) and entity
 *    positions (the `Entities` object layer) all live in the map, not
 *    in the code. Edit it in Tiled, re-run `gen_assets.py`, rebuild.
 *    The map is validated by the SDK's own `tmx2snes` converter.
 * 2. **A real bordered dialog box**: a 9-slice panel on BG2 with the
 *    text on BG3 above it — the classic SNES RPG window.
 *
 * Layer roles (Mode 1):
 * - BG1: the town (4bpp, 64x64 scrolling)
 * - BG2: the dialog box panel, shown only while talking
 * - BG3: the dialog text (2bpp, high priority)
 * - OBJ: the hero and the villager (same tiles, two palettes)
 *
 * ROM mode: LoROM (project default).
 *
 * @par SNES Concepts
 * - Tiled (.tmj) as the content pipeline: collision and entities are
 *   data, not hardcoded
 * - Tile-exact collision: the hero OCCUPIES one tile and its 16x16
 *   sprite is drawn straddling it (feet on the tile), so what you see
 *   is what collides — the classic top-down RPG convention
 * - A 9-slice dialog box DMA'd to BG2 on open, with layer priorities
 *   stacking town < box < text
 * - Collision through the SDK's `collideTile()` over the Tiled map —
 *   its `const` tilemap parameter means the read is bank-honouring
 *   (#121), so a multi-KB map needs neither bank $00 nor RAM
 *
 * @par What to Observe
 * The town fades in. Walk with the D-pad — houses, water, trees and
 * the fence block you exactly where they look. Face the villager and
 * press A, or step onto the chest and press A.
 *
 * @par Modules Used
 * console, dma, background, sprite, text, input
 *
 * @see gen_assets.py, res/town.tmj — the Tiled content pipeline
 */

#include <snes.h>
#include <snes/background.h>
#include <snes/sprite.h>
#include <snes/text.h>
#include <snes/input.h>
#include <snes/collision.h>

#include "res/entities.inc"     /* SPAWN/NPC/CHEST tile coords from the .tmj */

extern u8 town_tiles[], town_tiles_end[];
extern u8 town_map[];
extern u8 town_pal[];
extern const u8 town_collision[];      /* const -> far reads (#121) */
extern u8 hero_tiles[], hero_tiles_end[];
extern u8 hero_pal[];
extern u8 npc_pal[];
extern u8 ui_tiles[], ui_tiles_end[];
extern u8 ui_pal[];

/* VRAM word layout */
#define VRAM_TOWN_TILES 0x0000
#define VRAM_TOWN_MAP   0x2000
#define VRAM_FONT       0x3000
#define VRAM_TEXT_MAP   0x3800
#define VRAM_UI_TILES   0x4000
#define VRAM_UI_MAP     0x4400
#define VRAM_HERO       0x6000
#define OBJ_NAME_BASE   3      /* base 3 x $2000 words = VRAM_HERO */

/* 9-slice box tiles (uibox.png is 3x3 of 8x8, raster order) */
#define BOX_TL 0
#define BOX_T  1
#define BOX_TR 2
#define BOX_L  3
#define BOX_C  4
#define BOX_R  5
#define BOX_BL 6
#define BOX_B  7
#define BOX_BR 8
#define BOX_PAL 2                 /* BG2 palette 2 -> CGRAM 32-47 */

/* Dialog panel geometry (BG2 tilemap, 32x32) */
#define PANEL_X 2
#define PANEL_Y 22
#define PANEL_W 28
#define PANEL_H 6
#define TEXT_W  (PANEL_W - 4)     /* usable characters per line */

#define FACE_DOWN 0
#define FACE_UP   1
#define FACE_LEFT 2
#define FACE_RIGHT 3

#define MAP_TILES 64
#define WORLD_W (MAP_TILES * 8)
#define WORLD_H (MAP_TILES * 8)
#define SCREEN_CX 124             /* where the hero tile sits on screen */
#define SCREEN_CY 108

enum { ST_FADEIN, ST_EXPLORE, ST_DIALOG };

/** @brief Probe oracles / state. hero_x/hero_y are the PIXEL position
 * of the tile the hero occupies (a multiple of 8 when idle). */
u16 hero_x;
u16 hero_y;
u8  hero_facing;
u8  game_state;
u8  chest_opened;

static s8 step_dx, step_dy;
static u8 step_count;
static u8 walk_phase, anim_tick;
static u8 fade_level;
static u16 panel_map[32 * 32];

/* ---- collision: the SDK's collideTile() over the Tiled map ----
 * collideTile takes PIXEL coordinates, converts to 8x8 tiles and
 * returns the map byte (nonzero = solid). It bounds X and negative
 * coordinates itself (off-map = solid) but cannot bound Y — it does
 * not know the map height — so we clamp that here, as its docs ask.
 * Its `tilemap` parameter is const, so the read is bank-honouring:
 * our 4 KB map lives outside bank $00. */
static u8 tile_walkable(u16 tx, u16 ty) {
    if (ty >= MAP_TILES) {
        return 0;
    }
    return (u8)(collideTile((s16)(tx << 3), (s16)(ty << 3),
                            town_collision, MAP_TILES) == 0);
}

static u16 hero_tx(void) { return (u16)(hero_x >> 3); }
static u16 hero_ty(void) { return (u16)(hero_y >> 3); }

static void front_tile(u16 *tx, u16 *ty) {
    u16 cx = hero_tx();
    u16 cy = hero_ty();
    switch (hero_facing) {
    case FACE_UP:    cy--; break;
    case FACE_DOWN:  cy++; break;
    case FACE_LEFT:  cx--; break;
    case FACE_RIGHT: cx++; break;
    default: break;
    }
    *tx = cx;
    *ty = cy;
}

/* ---- the dialog box ---- */
static void build_panel(void) {
    u16 x, y, i;
    u16 pal = (u16)(BOX_PAL << 10) | 0x2000;   /* +priority: over the town */
    for (i = 0; i < 32 * 32; i++) {
        panel_map[i] = 0;
    }
    for (y = 0; y < PANEL_H; y++) {
        for (x = 0; x < PANEL_W; x++) {
            u16 t;
            if (y == 0) {
                t = (x == 0) ? BOX_TL : (x == PANEL_W - 1) ? BOX_TR : BOX_T;
            } else if (y == PANEL_H - 1) {
                t = (x == 0) ? BOX_BL : (x == PANEL_W - 1) ? BOX_BR : BOX_B;
            } else {
                t = (x == 0) ? BOX_L : (x == PANEL_W - 1) ? BOX_R : BOX_C;
            }
            panel_map[(PANEL_Y + y) * 32 + (PANEL_X + x)] = (u16)(t | pal);
        }
    }
}

static void dialog_open(const char *line) {
    setBrightness(0);
    dmaCopyVram((u8 *)panel_map, VRAM_UI_MAP, 32 * 32 * 2);
    setBrightness(15);
    textPrintAt(PANEL_X + 2, PANEL_Y + 2, line);
    textPrintAt(PANEL_X + 2, PANEL_Y + 4, "         (A) OK");
    setMainScreen(TM_BG1 | TM_BG2 | TM_BG3 | LAYER_OBJ);
    game_state = ST_DIALOG;
}

static void dialog_close(void) {
    textClear();
    setMainScreen(TM_BG1 | TM_BG3 | LAYER_OBJ);
    game_state = ST_EXPLORE;
}

/* ---- movement: one tile per step, slid over 8 frames ---- */
static void begin_step(s8 dx, s8 dy) {
    u16 ntx = (u16)(hero_tx() + dx);
    u16 nty = (u16)(hero_ty() + dy);
    if (tile_walkable(ntx, nty)) {
        step_dx = dx;
        step_dy = dy;
        step_count = 8;
    }
}

/**
 * @brief Draw a 16x16 character straddling its 8x8 tile.
 *
 * THIS is what makes collision feel exact: the character's logical
 * position is one tile; the sprite is drawn 4 px left and 8 px up of
 * it, so its feet stand on that tile and its body overhangs upward —
 * the standard top-down RPG convention. Drawing the sprite at the
 * tile's corner instead (the naive version) puts the visible body
 * half a tile away from what collides.
 */
static void draw_char(u8 oam_id, u16 wx, u16 wy, u16 cam_x, u16 cam_y,
                      u8 facing, u8 phase, u8 palette) {
    u16 sx = (u16)(wx - cam_x - 4);
    u16 sy = (u16)(wy - cam_y - 8);
    u8 f = (u8)(facing * 4 + phase * 2);
    oamSet(oam_id, sx, sy, f, palette, 2, 0);
    oamSetSize(oam_id, OBJ_LARGE);
}

int main(void) {
    u16 keys, cam_x, cam_y;
    u8 moving;

    consoleInit();
    setMode(BG_MODE1, 0x08);               /* BG3 high priority */

    /* BG1 town, from the Tiled map */
    dmaCopyVram(town_tiles, VRAM_TOWN_TILES, (u16)(town_tiles_end - town_tiles));
    dmaCopyVram(town_map, VRAM_TOWN_MAP, 8192);
    bgSetGfxPtr(0, VRAM_TOWN_TILES);
    bgSetMapPtr(0, VRAM_TOWN_MAP, SC_64x64);

    /* BG3 text overlay */
    textInit(VRAM_TEXT_MAP, 0, 4);
    text_config.priority = 1;
    textLoadFont(VRAM_FONT);
    bgSetGfxPtr(2, VRAM_FONT);
    bgSetMapPtr(2, VRAM_TEXT_MAP, SC_32x32);
    setColor(4 * 4 + 1, RGB(31, 31, 31));

    /* BG2 dialog box */
    dmaCopyVram(ui_tiles, VRAM_UI_TILES, (u16)(ui_tiles_end - ui_tiles));
    dmaCopyCGram(ui_pal, BOX_PAL * 16, 32);
    bgSetGfxPtr(1, VRAM_UI_TILES);
    bgSetMapPtr(1, VRAM_UI_MAP, SC_32x32);
    build_panel();

    /* OBJ: hero (palette 0) and villager (palette 1) share the tiles */
    dmaCopyVram(hero_tiles, VRAM_HERO, (u16)(hero_tiles_end - hero_tiles));
    dmaCopyCGram(hero_pal, 128, 32);       /* OBJ palette 0 */
    dmaCopyCGram(npc_pal, 144, 32);        /* OBJ palette 1 */
    oamInit(OBJ_SIZE8_L16, OBJ_NAME_BASE);

    /* spawn from the Tiled Entities layer */
    hero_x = SPAWN_TX * 8;
    hero_y = SPAWN_TY * 8;
    hero_facing = FACE_UP;
    chest_opened = 0;

    /* town palette LAST (textInit/textLoadFont clear CGRAM 0-15) */
    dmaCopyCGram(town_pal, 0, 32);

    setMainScreen(TM_BG1 | TM_BG3 | LAYER_OBJ);
    setBrightness(0);
    setScreenOn();
    game_state = ST_FADEIN;
    fade_level = 0;

    while (1) {
        WaitForVBlank();
        keys = padHeld(0);
        moving = 0;

        if (game_state == ST_FADEIN) {
            fade_level++;
            setBrightness((u8)(fade_level >> 1));
            if (fade_level >= 30) {
                setBrightness(15);
                game_state = ST_EXPLORE;
            }
        } else if (game_state == ST_EXPLORE) {
            if (step_count > 0) {
                hero_x = (u16)(hero_x + step_dx);
                hero_y = (u16)(hero_y + step_dy);
                step_count--;
                moving = 1;
            } else {
                if (keys & KEY_UP) {
                    hero_facing = FACE_UP; begin_step(0, -1); moving = 1;
                } else if (keys & KEY_DOWN) {
                    hero_facing = FACE_DOWN; begin_step(0, 1); moving = 1;
                } else if (keys & KEY_LEFT) {
                    hero_facing = FACE_LEFT; begin_step(-1, 0); moving = 1;
                } else if (keys & KEY_RIGHT) {
                    hero_facing = FACE_RIGHT; begin_step(1, 0); moving = 1;
                } else if (padPressed(0) & KEY_A) {
                    u16 ftx, fty;
                    front_tile(&ftx, &fty);
                    if (ftx == NPC_TX && fty == NPC_TY) {
                        dialog_open("WELCOME, TRAVELER!");
                    } else if ((hero_tx() == CHEST_TX && hero_ty() == CHEST_TY)
                               || (ftx == CHEST_TX && fty == CHEST_TY)) {
                        if (chest_opened) {
                            dialog_open("THE CHEST IS EMPTY.");
                        } else {
                            chest_opened = 1;
                            dialog_open("YOU FOUND 10 GOLD!");
                        }
                    }
                }
            }
        } else {                            /* ST_DIALOG */
            if (padPressed(0) & KEY_A) {
                dialog_close();
            }
        }

        if (moving) {
            anim_tick++;
            if (anim_tick >= 8) { anim_tick = 0; walk_phase ^= 1; }
        } else {
            walk_phase = 0;
        }

        /* camera centred on the hero tile, clamped to the world */
        cam_x = (hero_x > SCREEN_CX) ? (u16)(hero_x - SCREEN_CX) : 0;
        cam_y = (hero_y > SCREEN_CY) ? (u16)(hero_y - SCREEN_CY) : 0;
        if (cam_x > WORLD_W - 256) { cam_x = WORLD_W - 256; }
        if (cam_y > WORLD_H - 224) { cam_y = WORLD_H - 224; }
        bgSetScroll(0, cam_x, cam_y);

        draw_char(0, hero_x, hero_y, cam_x, cam_y, hero_facing, walk_phase, 0);
        /* the villager: same tiles, palette 1, facing the crossroads */
        draw_char(1, NPC_TX * 8, NPC_TY * 8, cam_x, cam_y, FACE_DOWN, 0, 1);
    }

    return 0;
}
