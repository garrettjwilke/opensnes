# C1 — audit des modules 100 % ASM (migration mesurée vers le C)

Status: Phase 0 (spec + instrument) — 2026-07-19. Owner-validated as
a structural priority ("précisément l'esprit de ce projet").
Catalogue entry: `.claude/STRUCTURAL_DEFECTS.md` C1. Precedent:
`audio.asm` (was the worst of five) retired by the audio v2 chantier
— not migrated but rebuilt in C on a measured foundation.

## The rule of the game (from the catalogue, non-negotiable)

Every keep/migrate decision is **benchmark-backed**. Migrate when the
C port regresses ≤ +10 % on the module's hot paths (maintainability
wins); keep ASM when it doesn't, extracting cold paths to C and
documenting the hot-path boundary + internal invariants. No decision
by intuition — the QBE backend has had 28 perf patches since the ASM
was written; the gap must be re-measured, both directions.

## Inventory (2026-07-19)

| Module | LOC | Public fns | Linked by | First read |
|---|---|---|---|---|
| `mode7.asm` | 481 | 10 (`mode7.h`) | 3 examples | smallest, well-bounded — **first target** |
| `sprite_dynamic.asm` | 1232 | via `sprite.h` (dispatch + helpers split) | 5 examples | hot NMI-adjacent paths (VRAM queue) |
| `map.asm` | 1240 | 7 | 4 examples | collision getters already partly C-fronted (#103) |
| `snesmod.asm` | 1201 | 16 | snesmod_* via dedicated wiring | mukunda's driver port — expected verdict: KEEP + document (upstream parity > rewrite) |

Order: **mode7 → sprite_dynamic → map → snesmod**. Each module = one
wip branch, one squash-merge, decision recorded here.

## Phase 0 deliverable: the instrument (benchrom)

A cycle-benchmark fixture, libtest-style: `devtools/benchrom/` — a
ROM that runs each measured function in a tight N-iteration loop
bracketed by frame counters, storing per-function frame counts in
WRAM globals; a python runner (luna `state --peek`, symbol names)
converts to ~cycles/call and prints a table. Relative precision is
what matters (same harness measures the ASM original and the C port);
absolute cycles are informative only. The instrument lands BEFORE any
migration and each module's baseline numbers are committed to this
note — the same discipline as #120's provenance: no "it felt fast".

Measurement points per module = its hot paths as named by the
catalogue + the functions its examples actually call (grep-derived,
not guessed).

## Baselines — mode7.asm (ASM original, 2026-07-19)

Instrument: `devtools/benchrom` (20 000 iterations/fn, frame-count
brackets, empty-loop calibration 53.6 cyc/iter subtracted; ~59 561
CPU cycles/frame NTSC). `python3 devtools/benchrom/bench.py`.

| Function | frames | ~cycles/call |
|---|---|---|
| mode7SetAngle | 203 | **551** |
| mode7SetScale | 47 | 86 |
| mode7SetCenter | 53 | 104 |
| mode7SetMatrix | 73 | 164 |
| mode7Transform | 275 | **765** |

Per-frame budget context: an example calling SetAngle every frame
spends 551/59 561 ≈ 0.9 % of the frame in it. The +10 % rule
therefore allows the C port ≈ 606 cyc for SetAngle, ≈ 842 for
Transform. The cheap register-stuffers (SetScale/Center/Matrix) are
prime C candidates; the trig paths (SetAngle/Transform) are where
the LUT indexing quality of the codegen will decide.

## Acceptance criteria (per module, from the catalogue)

1. A written keep/migrate decision in this note with the benchmark
   table (ASM vs C, cycles/call, regression %).
2. Migrated: suite + probes green, WRAM/fbhash rebaselines documented,
   ROM-sha churn justified, ≤ +10 % on hot paths.
3. Kept: cold paths extracted to C where sensible; an internal-
   invariants doc (struct offsets, register state, .ACCU discipline)
   in the module header; ABI-lint coverage confirmed.
4. End of chantier: `lib/ARCHITECTURE.md` (or PHILOSOPHY.md section)
   states the C/ASM split policy going forward.

## Risks

- **NMI-adjacent paths** (sprite_dynamic's queue flush): regressions
  here eat VBlank budget — the +10 % rule applies to the VBlank-
  critical section specifically, not the average.
- **snesmod**: upstream parity is worth more than C purity; the audit
  may legitimately conclude "keep everything, document". That is a
  SUCCESS outcome, not a failure of the chantier.
- **Baseline churn**: every migration shifts return addresses → WRAM
  stream rebaselines. Clean-build discipline (clock-skew machine!)
  and per-phase ROM-invariance checks as in audio v2.
