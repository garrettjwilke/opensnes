# lib architecture — the C / ASM split policy

Outcome of the C1 audit (2026-07, `.claude/notes/chantiers/c1_asm_audit.md`).
This is the normative answer to "should this lib code be C or 65816
assembly?" — every past decision below is benchmark-backed, and future
ones must be too.

## The policy

**Default: C.** New lib code is written in C unless it falls into one
of the three measured ASM niches below. The cc65816 toolchain earns
this default: the codegen is within ±15 % of hand ASM on
register-stuffing and pure-RAM state code (mode7's `SetScale` is
*faster* in C), the ABI lint cannot even apply to C (nothing to
verify), and every compiled function is one less bus-factor liability.

**ASM is reserved for three profiles**, each demonstrated by an audit
verdict:

1. **DB-pinned bulk-state engines** (`map.asm`): ≥ multiple KB of
   state deliberately placed in bank $7E above the C-visible WRAM
   mirror, accessed at 5-6 cycles via the data-bank register. Compiled
   C must use 4-byte far pointers (~13 cycles/access): measured
   **+33 % on the lightest possible path** before the buffer loops.
2. **Register-pinned per-frame inner loops** (`sprite_dynamic.asm`):
   functions that keep X/Y/DB pinned across dozens of accesses.
   Compiled C re-derives every array index per statement — a
   register-allocation gap, not fixable by peepholes. Measured
   **~+50 %** on the steady draw path, multiplied by sprite count per
   frame, part of it inside the NMI's VBlank budget.
3. **Upstream-parity code** (`snesmod.asm`): foreign drivers whose
   value is behavioural identity with their ecosystem. Kept on
   provenance, no benchmark needed.

## The process (non-negotiable)

- Measure with `devtools/benchrom` (frame-count brackets, empty-loop
  calibration) BEFORE and AFTER. Same harness both sides.
- Migration passes at **≤ +10 %** on the hot paths. Exceptions must be
  argued in absolute cycles on the real per-frame callers and recorded
  in the audit note (mode7's trig paths: +39 % relative but 0.36 % of
  a frame absolute — migrated by owner decision).
- On the NMI/VBlank path the rule is strict — no absolute-cost
  exceptions (the budget is ~4 KB DMA + a fixed cycle window, shared).
- A migration should pay its way in compiler improvements: mode7's
  port forced the byte-pair store fusion (~18 cycles saved on every
  16-bit MMIO write, corpus-wide) and exposed two silent-miscompilation
  bugs. If the port only fights the emitter, stop and file the emitter
  gap instead.
- Kept-ASM modules carry their invariants in the file header (DB
  discipline, register pinning, NMI budget, C-contract struct layouts)
  and stay under the ABI lint.

## Current split (post-audit)

| Module | Form | Rationale |
|---|---|---|
| mode7 | **C** (migrated 2026-07-19) | register stuffing + PPU multiplier; fusion closed most of the gap |
| audio v2 | **C** + SPC700 driver from source | new engine (replaced broken-ABI audio.asm) |
| map | ASM (verdict KEEP) | profile 1 — bank-$7E bulk state |
| sprite_dynamic | ASM core + C cold paths | profile 2 — pinned-register inner loops; dispatch/helpers/meta already C |
| snesmod | ASM (verdict KEEP) | profile 3 — upstream parity (mukunda's driver) |
| everything else | C | the default |

## Future emitter work that could move these lines

Recorded, not scheduled: a "pinned data bank" region concept would
close map's far-access gap; cross-statement register allocation for
array indices would close sprite_dynamic's. Either landing justifies
re-running the audit for its module — benchrom keeps the baselines.
