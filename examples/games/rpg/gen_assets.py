#!/usr/bin/env python3
"""Generate every RPG asset from a Tiled map (the source of truth).

Outputs in res/:
  tileset.png            — 16 unique 8x8 terrain tiles (gfx4snes input)
  town.tmj               — the Tiled map: terrain + per-tile collision
                           + an Entities object layer (spawn/npc/chest)
  town_map.bin           — SNES 64x64 tilemap (4 quadrant pages) from the .tmj
  town_collision.bin     — 64x64 bytes, 0 walkable / 1 blocked, from the .tmj
  entities.inc           — C defines for the entity tile positions
  hero.png               — 8-frame walk sheet (4 dirs x 2)
  uibox.png              — 3x3 bordered dialog-box tileset

EDIT THE MAP IN TILED: open res/town.tmj, edit terrain / drag the
Entities objects, then re-run this script (it re-reads the .tmj and
regenerates only the binaries) and `make`.

Run:  python3 gen_assets.py            # regenerate .tmj too (from scratch)
      python3 gen_assets.py --keep-map # keep an edited town.tmj, rebuild bins
"""
from pathlib import Path
import json
import sys

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
RES = HERE / "res"
RES.mkdir(exist_ok=True)

MAP_W = MAP_H = 64            # tiles
TILE = 8

# --- terrain tile table: (name, color, blocked) --------------------
# Tile index in the tileset == index here. Tiled gid = index + 1.
TILES = [
    ("grass",   (56, 132, 48),  0),
    ("grass2",  (48, 120, 44),  0),
    ("path",    (168, 140, 96), 0),
    ("wall",    (176, 148, 112), 1),
    ("roof",    (168, 64, 56),  1),
    ("door",    (64, 40, 24),   0),
    ("water",   (48, 96, 176),  1),
    ("tree",    (32, 88, 34),   1),
    ("fence",   (120, 92, 60),  1),
    ("chest",   (150, 96, 40),  0),   # walkable: you step on it
    ("flower",  (72, 148, 60),  0),
    ("sand",    (196, 176, 128), 0),
]
NAME2IDX = {n: i for i, (n, _, _) in enumerate(TILES)}

SPAWN = (31, 34)
NPC = (34, 31)
CHEST = (31, 28)


# ---------------------------------------------------------------- tileset
def gen_tileset():
    """One unique 8x8 tile per terrain type, laid out in a 16-wide row."""
    img = Image.new("RGB", (16 * TILE, TILE))
    d = ImageDraw.Draw(img)
    for i, (name, color, _) in enumerate(TILES):
        x0 = i * TILE
        d.rectangle([x0, 0, x0 + 7, 7], fill=color)
        # a little texture so the tiles read as terrain, not flat squares
        if name in ("grass", "grass2", "flower"):
            d.point((x0 + 2, 2), fill=(color[0] + 14, color[1] + 14, color[2]))
            d.point((x0 + 5, 5), fill=(color[0] + 14, color[1] + 14, color[2]))
            if name == "flower":
                d.point((x0 + 4, 3), fill=(230, 220, 90))
        elif name == "path":
            d.point((x0 + 1, 4), fill=(190, 165, 120))
            d.point((x0 + 6, 2), fill=(190, 165, 120))
        elif name == "wall":
            d.line([(x0, 3), (x0 + 7, 3)], fill=(140, 116, 88))
            d.line([(x0 + 3, 0), (x0 + 3, 3)], fill=(140, 116, 88))
        elif name == "roof":
            d.line([(x0, 2), (x0 + 7, 2)], fill=(130, 44, 40))
            d.line([(x0, 5), (x0 + 7, 5)], fill=(130, 44, 40))
        elif name == "water":
            d.line([(x0 + 1, 2), (x0 + 4, 2)], fill=(90, 150, 220))
            d.line([(x0 + 3, 5), (x0 + 6, 5)], fill=(90, 150, 220))
        elif name == "tree":
            d.ellipse([x0 + 1, 0, x0 + 6, 5], fill=(48, 118, 46))
            d.rectangle([x0 + 3, 6, x0 + 4, 7], fill=(70, 44, 20))
        elif name == "fence":
            d.rectangle([x0 + 1, 1, x0 + 6, 6], fill=(150, 118, 78))
            d.line([(x0 + 1, 3), (x0 + 6, 3)], fill=(96, 72, 44))
        elif name == "chest":
            d.rectangle([x0 + 1, 2, x0 + 6, 7], fill=(150, 96, 40))
            d.rectangle([x0 + 1, 2, x0 + 6, 3], fill=(96, 60, 22))
            d.point((x0 + 3, 5), fill=(240, 210, 70))
            d.point((x0 + 4, 5), fill=(240, 210, 70))
    img.convert("P", palette=Image.ADAPTIVE, colors=16).save(RES / "tileset.png")
    print(f"tileset.png ({len(TILES)} terrain tiles)")


# ------------------------------------------------------------- tiled map
def build_terrain():
    """Return a MAP_H x MAP_W grid of tile indices (the town layout)."""
    g = NAME2IDX["grass"]
    g2 = NAME2IDX["grass2"]
    grid = [[g if (x + y) % 2 else g2 for x in range(MAP_W)]
            for y in range(MAP_H)]

    def rect(x0, y0, x1, y1, name):
        t = NAME2IDX[name]
        for y in range(max(0, y0), min(MAP_H, y1 + 1)):
            for x in range(max(0, x0), min(MAP_W, x1 + 1)):
                grid[y][x] = t

    # fence border (1 tile ring)
    rect(0, 0, MAP_W - 1, 0, "fence")
    rect(0, MAP_H - 1, MAP_W - 1, MAP_H - 1, "fence")
    rect(0, 0, 0, MAP_H - 1, "fence")
    rect(MAP_W - 1, 0, MAP_W - 1, MAP_H - 1, "fence")

    # cross roads (2 tiles wide) through the middle
    rect(8, 31, MAP_W - 9, 32, "path")
    rect(31, 8, 32, MAP_H - 9, "path")

    # pond (top-right)
    rect(46, 10, 55, 17, "water")
    rect(45, 12, 45, 15, "water")
    rect(56, 12, 56, 15, "water")

    # three houses: 4x3 wall block with a roof row and a door
    def house(x, y):
        rect(x, y, x + 3, y, "roof")
        rect(x, y + 1, x + 3, y + 2, "wall")
        rect(x + 1, y + 2, x + 1, y + 2, "door")
    house(14, 37)
    house(40, 18)
    house(41, 40)

    # tree clusters
    for (tx, ty) in [(12, 16), (13, 17), (16, 18), (11, 19),
                     (48, 45), (49, 46), (52, 44), (46, 48),
                     (17, 50), (18, 51), (15, 52)]:
        rect(tx, ty, tx, ty, "tree")

    # flower patches
    for (fx, fy) in [(24, 24), (25, 25), (38, 26), (39, 27), (22, 40)]:
        rect(fx, fy, fx, fy, "flower")

    # chest tile (walkable — you step on it to open)
    rect(CHEST[0], CHEST[1], CHEST[0], CHEST[1], "chest")

    # sand patch around the spawn so the start reads clearly
    rect(SPAWN[0] - 1, SPAWN[1], SPAWN[0] + 1, SPAWN[1], "sand")
    return grid


def gen_tmj(grid):
    """Write a real Tiled map (editable in Tiled) with per-tile collision
    properties and an Entities object layer."""
    tiles_props = []
    for i, (name, _, blocked) in enumerate(TILES):
        tiles_props.append({
            "id": i,
            "properties": [
                {"name": "attribute", "type": "string",
                 "value": "FF00" if blocked else "0"},
                {"name": "palette", "type": "string", "value": "0"},
                {"name": "priority", "type": "string", "value": "0"},
                {"name": "terrain", "type": "string", "value": name},
            ],
        })

    data = []
    for y in range(MAP_H):
        for x in range(MAP_W):
            data.append(grid[y][x] + 1)      # Tiled gids are 1-based

    tmj = {
        "compressionlevel": -1, "infinite": False,
        "width": MAP_W, "height": MAP_H, "tilewidth": TILE, "tileheight": TILE,
        "orientation": "orthogonal", "renderorder": "right-down",
        "tiledversion": "1.10.2", "type": "map", "version": "1.10",
        "nextlayerid": 3, "nextobjectid": 4,
        "layers": [
            {"id": 1, "name": "BG1", "type": "tilelayer",
             "width": MAP_W, "height": MAP_H, "x": 0, "y": 0,
             "opacity": 1, "visible": True, "data": data},
            {"id": 2, "name": "Entities", "type": "objectgroup",
             "x": 0, "y": 0, "opacity": 1, "visible": True,
             "draworder": "topdown",
             "objects": [
                 {"id": 1, "name": "spawn", "type": "spawn", "rotation": 0,
                  "x": SPAWN[0] * TILE, "y": SPAWN[1] * TILE,
                  "width": TILE, "height": TILE, "visible": True},
                 {"id": 2, "name": "villager", "type": "npc", "rotation": 0,
                  "x": NPC[0] * TILE, "y": NPC[1] * TILE,
                  "width": TILE, "height": TILE, "visible": True},
                 {"id": 3, "name": "chest", "type": "chest", "rotation": 0,
                  "x": CHEST[0] * TILE, "y": CHEST[1] * TILE,
                  "width": TILE, "height": TILE, "visible": True},
             ]},
        ],
        "tilesets": [{
            "firstgid": 1, "name": "tileset", "image": "tileset.png",
            "imagewidth": 16 * TILE, "imageheight": TILE,
            "tilewidth": TILE, "tileheight": TILE,
            "tilecount": 16, "columns": 16, "margin": 0, "spacing": 0,
            "tiles": tiles_props,
        }],
    }
    # cute_tiled (tmx2snes' parser) needs COMPACT json with sorted keys
    (RES / "town.tmj").write_text(
        json.dumps(tmj, sort_keys=True, separators=(",", ":")))
    print("town.tmj (editable in Tiled: terrain + collision + entities)")


# ------------------------------------------------- tmj -> SNES binaries
def convert_tmj():
    """Read the Tiled map and emit the SNES tilemap, the collision map
    and the entity positions. This is the step to re-run after editing
    the map in Tiled."""
    tmj = json.loads((RES / "town.tmj").read_text())
    w, h = tmj["width"], tmj["height"]
    layer = next(l for l in tmj["layers"] if l["type"] == "tilelayer")
    ts = tmj["tilesets"][0]
    first = ts["firstgid"]

    blocked = {}
    for t in ts.get("tiles", []):
        attr = next((p["value"] for p in t.get("properties", [])
                     if p["name"] == "attribute"), "0")
        blocked[t["id"]] = 1 if attr != "0" else 0

    # SNES 64x64 tilemap = four 32x32 quadrant pages (TL, TR, BL, BR)
    tmap = bytearray()
    for qy in range(2):
        for qx in range(2):
            for ty in range(32):
                for tx in range(32):
                    gx, gy = qx * 32 + tx, qy * 32 + ty
                    idx = layer["data"][gy * w + gx] - first
                    tmap += bytes((idx & 0xFF, (idx >> 8) & 0x03))
    (RES / "town_map.bin").write_bytes(bytes(tmap))

    coll = bytearray(w * h)
    for i, gid in enumerate(layer["data"]):
        coll[i] = blocked.get(gid - first, 0)
    (RES / "town_collision.bin").write_bytes(bytes(coll))

    ents = {}
    for l in tmj["layers"]:
        if l["type"] == "objectgroup":
            for o in l["objects"]:
                ents[o["type"]] = (o["x"] // TILE, o["y"] // TILE)

    # An NPC entity blocks its tile (you talk to it face-to-face); a
    # chest does not (you step onto it). Entity-driven, from the map.
    if "npc" in ents:
        nx, ny = ents["npc"]
        coll[ny * w + nx] = 1
    (RES / "town_collision.bin").write_bytes(bytes(coll))

    inc = ["/* Generated from res/town.tmj by gen_assets.py — do not edit. */"]
    for key, macro in (("spawn", "SPAWN"), ("npc", "NPC"), ("chest", "CHEST")):
        tx, ty = ents.get(key, (0, 0))
        inc.append(f"#define {macro}_TX {tx}")
        inc.append(f"#define {macro}_TY {ty}")
    (RES / "entities.inc").write_text("\n".join(inc) + "\n")
    print(f"town_map.bin + town_collision.bin (blocked {sum(coll)}/{w*h}) "
          f"+ entities.inc {ents}")


# ------------------------------------------------------------------ hero
def _quantize_with_transparent(img, out_path):
    q = img.convert("P", palette=Image.ADAPTIVE, colors=15)
    src = list(q.get_flattened_data())
    ncol = max(src) + 1
    flat = q.getpalette()
    pal = [tuple(flat[i * 3:i * 3 + 3]) for i in range(ncol)]
    mag = min(range(ncol),
              key=lambda i: (pal[i][0] - 255) ** 2 + pal[i][1] ** 2
              + (pal[i][2] - 255) ** 2)
    remap, nxt, newpal = {}, 1, [255, 0, 255]
    for i in range(ncol):
        if i == mag:
            remap[i] = 0
        else:
            remap[i] = nxt
            newpal += list(pal[i])
            nxt += 1
    newpal += [0, 0, 0] * (16 - nxt)
    out = Image.new("P", img.size)
    out.putdata(bytes(remap[v] for v in src))
    out.putpalette(newpal)
    out.save(out_path)


def gen_hero():
    SKIN = (240, 200, 160); HAIR = (96, 56, 24); SHIRT = (56, 96, 200)
    PANTS = (48, 48, 72); SHOE = (32, 24, 16)
    img = Image.new("RGB", (128, 16), (255, 0, 255))
    d = ImageDraw.Draw(img)

    def frame(ox, facing, step):
        d.rectangle([ox + 5, 1, ox + 10, 5], fill=SKIN)
        if facing == "up":
            d.rectangle([ox + 5, 1, ox + 10, 3], fill=HAIR)
        elif facing == "down":
            d.rectangle([ox + 5, 1, ox + 10, 2], fill=HAIR)
            d.point((ox + 6, 4), fill=(0, 0, 0)); d.point((ox + 9, 4), fill=(0, 0, 0))
        else:
            d.rectangle([ox + 5, 1, ox + 10, 2], fill=HAIR)
            d.point((ox + 6 if facing == "left" else ox + 9, 4), fill=(0, 0, 0))
        d.rectangle([ox + 4, 6, ox + 11, 11], fill=SHIRT)
        l0, l1 = (12, 14) if step == 0 else (14, 12)
        d.rectangle([ox + 5, 12, ox + 7, l0], fill=PANTS)
        d.rectangle([ox + 8, 12, ox + 10, l1], fill=PANTS)
        d.rectangle([ox + 5, l0 - 1, ox + 7, l0], fill=SHOE)
        d.rectangle([ox + 8, l1 - 1, ox + 10, l1], fill=SHOE)

    f = 0
    for facing in ("down", "up", "left", "right"):
        for step in range(2):
            frame(f * 16, facing, step)
            f += 1
    _quantize_with_transparent(img, RES / "hero.png")
    print("hero.png (8 frames)")


def gen_npc_palette():
    """The NPC reuses the hero's TILES with a different palette: green
    tunic, grey hair, darker skin. Emitted as a raw 16-color SNES
    palette matching the hero palette's slot order."""
    hero = Image.open(RES / "hero.png")
    pal = hero.getpalette()[:16 * 3]
    # map hero colors -> npc colors (same slots, recolored)
    # Deliberately NOT green: a green villager disappears against the
    # grass. Purple/red reads instantly on every terrain tile.
    swap = {
        (56, 96, 200): (176, 64, 152),     # shirt  blue -> magenta robe
        (48, 48, 72): (88, 40, 80),        # pants  -> dark plum
        (96, 56, 24): (236, 220, 140),     # hair   -> blond
        (240, 200, 160): (216, 172, 132),  # skin   -> darker
    }
    out = bytearray()
    for i in range(16):
        r, g, b = pal[i * 3:i * 3 + 3]
        r, g, b = swap.get((r, g, b), (r, g, b))
        # SNES 15-bit BGR
        v = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
        out += bytes((v & 0xFF, v >> 8))
    (RES / "npc.pal").write_bytes(bytes(out))
    print("npc.pal (hero tiles, recolored)")


# --------------------------------------------------------------- ui box
def gen_uibox():
    FILL = (40, 32, 72); LIGHT = (120, 108, 190)
    DARK = (20, 16, 44); EDGE = (200, 196, 240)
    img = Image.new("RGB", (24, 24), (255, 0, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 23, 23], fill=FILL)
    d.line([(0, 0), (23, 0)], fill=LIGHT); d.line([(0, 0), (0, 23)], fill=LIGHT)
    d.line([(1, 1), (22, 1)], fill=LIGHT); d.line([(1, 1), (1, 22)], fill=LIGHT)
    d.line([(23, 0), (23, 23)], fill=DARK); d.line([(0, 23), (23, 23)], fill=DARK)
    d.line([(22, 1), (22, 22)], fill=DARK); d.line([(1, 22), (22, 22)], fill=DARK)
    d.rectangle([3, 3, 20, 20], outline=EDGE)
    _quantize_with_transparent(img, RES / "uibox.png")
    print("uibox.png (3x3 border tiles)")


if __name__ == "__main__":
    keep = "--keep-map" in sys.argv
    gen_tileset()
    if not keep or not (RES / "town.tmj").exists():
        gen_tmj(build_terrain())
    else:
        print("town.tmj kept (edited in Tiled)")
    convert_tmj()
    gen_hero()
    gen_npc_palette()
    gen_uibox()
