# Chantier: OpenSNES ecosystem — game-craft docs + dev tools

Status: **in progress** (Phase 1 = game-craft docs), started 2026-08-02.
Full plan: was `~/.claude/plans/jolly-wiggling-pascal.md`. This note is the
durable in-repo copy so the roadmap survives.

## Why

The SDK is strong; the ecosystem around it is thin. Three parallel web
research passes (game-craft education · SNES SDK tooling/DX · modern
retro-dev ecosystems) converged: **no SNES resource owns the seam between
"I know the API" and "I made a game."** Strategy = **curate + bridge**: link
world-class universal material, own only the SNES-specific on-hardware layer,
grounded in cited numbers.

## Track A — game-craft docs (`docs/craft/`)

Principles: link the universal / own the SNES-specific; every claim = a
number + a source (fullsnes, Anomie, SNESdev wiki, Bumbershoot); reference
`SNES_GRAPHICS_GUIDE.md` for hardware facts; anchor every guide to an
example; "constraints as creative fuel" framing.

- [Phase 1] `craft/README.md` — landing + curated "Go deeper" external library
- [Phase 1] `craft/planning.md` — VRAM/CGRAM/OAM budget worksheet +
  BG-mode-by-genre + first-game scoping (**biggest gap**)
- [Phase 1] `craft/backgrounds.md` — layer composition, priority, parallax, HUD
- [backlog] `craft/tiles-to-levels.md` — metatiles + Tiled (credit nesdoug/SMW Central)
- [backlog] `craft/frame-budget.md` — DMA/VBlank + sprite-per-line as design levers
- [backlog] per-technique "craft companions" (camera→Scroll Back+HDMA parallax;
  game-feel→Juice It+color-math/scroll-shake)

### Curated external library (link, don't rewrite)
- Cameras: Itay Keren *Scroll Back* — https://docs.google.com/document/d/1iNSQIyNpVGHeak6isbP6AHdHD50gs8MNXF1GCf08efg/pub
- Game feel: *Juice It or Lose It* (GDC) — https://www.gdcvault.com/play/1016487/Juice-It-or-Lose
- PPU "why": Fabien Sanglard — https://fabiensanglard.net/snes_ppus_why/
- Visual explainers: Retro Game Mechanics Explained — https://www.youtube.com/c/RetroGameMechanicsExplained
- Metatiles: nesdoug — https://nesdoug.com/2018/09/05/11-metatiles/
- Level design: SMW Central — https://www.smwcentral.net/?p=beginners
- Pixel-art palettes: 2D Will Never Die — https://2dwillneverdie.com/tutorial/so-you-want-your-sprites-to-be-16-colors/
- Hardware refs: fullsnes https://problemkaputt.de/fullsnes.htm · Anomie (romhacking.net/documents/199) · SNESdev https://snes.nesdev.org/wiki · Bumbershoot (DMA budget) https://bumbershootsoft.wordpress.com/2023/10/14/dma-and-fastrom-on-the-snes-speed-at-any-cost/

### Best-practice numbers (verified, citeable)
- CGRAM 256 colours (512 B); convention BG 0–127 / sprites 128–255.
- Tile cost: 2bpp=16 B, 4bpp=32 B, 8bpp=64 B. Tilemap 32×32 = 2 KB, entry = 2 B.
- VRAM 64 KB = one shared pool (tilesets + tilemaps + OBJ tiles).
- ~4 KB safe DMA per VBlank (theoretical ~6 KiB; source: Bumbershoot/SNESdev).
- OAM 128 sprites; 32 sprites / 34 8×8 slivers per scanline; off-screen X=−256
  still counts (PPU bug); sliver-cull from lowest OAM priority.
- Modes by genre: platformer→1, RPG→1/3, racer→7, puzzle→0, per-column FX→2.

## Track B — tools & DX (ranked value ÷ effort; reuse > build)

1. **BRR/SFX sample tool** — wrap BRRtools/snesbrr (MIT). smconv is music-only.
2. **Build-time VRAM/CGRAM budget report** — static check over pipeline outputs.
3. **`opensnes-starter`** — GitHub template repo + CI + one "sample project".
4. **`palplan`** — project shared-palette planner (build on tiledpalettequant,
   MIT; locked-index repack-on-demand). #1 community pain; highest value.
5. **`aseprite2snes`** — Aseprite CLI JSON (tags→frames) → metasprite+anim.
6. **DX**: watch/live-reload (fswatch→Mesen2), linker→Mesen2 C-symbol export,
   curate ~8–10 examples as annotated "study carts".

### Tool sources / licenses
- SuperFamiconv (MIT) https://github.com/Optiroc/SuperFamiconv
- tiledpalettequant (MIT) https://github.com/rilden/tiledpalettequant
- BRRtools (MIT) https://github.com/Optiroc/BRRtools · snesbrr https://github.com/boldowa/snesbrr
- Aseprite CLI `-b --data --list-tags` (or LibreSprite GPLv2 to avoid EULA)
- devkitSMS quickstart template https://github.com/retcon85/quickstart-sms-devkitsms
- SNES Studio (MIT, UX reference) https://www.snes-studio.com/

## Verification
Docs: `make docs` (pages resolve) + `check_doc_render.py` + `make lint-docs`.
Tools: per-tool goldens in `make test-tools`; example integration + luna.
