# Interactive Debugging with luna {#tutorial_debugging}

luna is not just the test harness — it is OpenSNES's interactive debugger.
Its headless CLI and its MCP server expose the full machine state (CPU, PPU,
APU, DMA, every memory space) to scripts and AI agents, with no Lua layer
and no external emulator required.

This guide shows the standard debugging workflows with the tools available
today. Symbol names, first-class breakpoints and MCP-side disassembly are
being added in luna (tracked upstream as luna issue #63); until then the
recipes below resolve symbols from the `.sym` file explicitly.

## Setup

The pinned binary is installed by the SDK:

```bash
scripts/install-luna.sh            # → tools/luna-test/bin/luna
```

Two ways to drive it:

- **CLI** — one-shot commands (`luna state`, `luna assets-dump`, …).
- **MCP** — a live debugging session for Claude Code or any MCP client:

```bash
claude mcp add luna -- tools/luna-test/bin/luna mcp
```

The MCP session exposes `load_rom`, `reset`, `step`, `step_until_frame`,
`state`, `screenshot`, `peek_memory`, `poke_memory`, `search_memory`,
`run_until_pc`, `run_until_mem_write`, `run_until_mem_read`,
`set_joypad`, `set_cpu_register`, `peek_vram`, `peek_aram`, `drain_audio`.

## Resolving a variable's address

Every build produces a wlalink `.sym` next to the ROM. Until luna ingests
it natively, look an address up directly:

```bash
grep -i ' monster_x$' examples/games/breakout/breakout.sym
# 00:0b3c monster_x        →  bank $00, offset $0B3C
```

(The Python probes use the same lookup — `sym_of()` in
`tools/luna-test/probes/lib.py`.)

## Recipe 1 — Inspect the machine at a point in time

```bash
tools/luna-test/bin/luna state -n 3000000 --screenshot /tmp/shot.png game.sfc
```

The JSON snapshot contains the full CPU registers, PPU state (scroll, mode,
windows, complete CGRAM and OAM), DMA channels, scheduler (frame/NMI
counts) — everything the Mesen2 watch panel showed, machine-readable.
Over MCP: `load_rom` → `step` → `state`.

## Recipe 2 — "Who writes this variable?"

The classic corruption hunt. Resolve the address (above), then over MCP:

1. `load_rom`, then `run_until_mem_write` with the bank/offset.
2. Execution stops right after the write: `state` gives the PC of the
   writing instruction; `peek_memory` confirms the new value.
3. Repeat `run_until_mem_write` to catch the next writer.

This replaces the old snesdbg `dbg.watch()` callback — the "callback" is
simply whatever you (or the agent driving MCP) do at each stop.

## Recipe 3 — Break at a function

Resolve the function's address from the `.sym`, then `run_until_pc`.
Single-step from there with `step` and read `state.cpu` at each step to
trace the execution path.

## Recipe 4 — Sprite/OAM debugging (shadow buffer vs hardware)

The SDK keeps a shadow OAM (`oambuffer`) that the NMI handler DMAs to the
PPU. When a sprite misbehaves, compare the two sides:

```bash
tools/luna-test/bin/luna assets-dump -n 3000000 --out /tmp/dump game.sfc
```

This writes `oam.json` (the hardware side, parsed), the sprite sheet, the
VRAM tile sheet, the four BG tilemaps, the CGRAM palette and the composited
screen as PNGs. Read the shadow side with `peek_memory` at the `oambuffer`
symbol and compare entry by entry — a mismatch means the OAM DMA didn't run
(check `oam_update_flag`) or wrote stale data.

`state` also embeds the full parsed OAM (`ppu.oam_full`), so a pure-MCP
comparison needs no file round-trip.

## Recipe 5 — Poke and observe

Test a hypothesis without rebuilding: `poke_memory` the variable,
`step_until_frame`, `screenshot`. Example: force a player's X position and
watch whether the sprite follows (if it doesn't, the shadow buffer isn't
being flushed).

## Recipe 6 — Temporal artefacts (flicker, page-flip desync)

A single screenshot can't show a frame-to-frame blink:

```bash
tools/luna-test/bin/luna frames -n 3000000 --count 8 --out /tmp/frames game.sfc
```

captures strictly consecutive PPU frames, each tagged with its frame number
and forced-blank state.

## What Mesen2 is still for

Nothing is *required* anymore: the workflows above cover the watch /
breakpoint / OAM / poke use cases headlessly. Third-party GUI emulators
(Mesen2, bsnes) remain fine for eyeballing gameplay, and Mesen2 stays cited
in the hardware docs as the accuracy reference that settled some SDK
behaviours (e.g. SA-1 SIWP polarity) — a historical role, not a dependency.
