# PeterLemon (krom) ports — the behavioral-parity validation pattern

**Rule: a port is validated by MEASURING both ROMs in luna, never by reading
the reference ASM alone.** The hdma_wave_table port shipped twice with
unfaithful behavior before this pattern existed: first with an invented
112-line wave period (reference: ~26), then with claims (scroll direction,
speed) derived from mechanism reasoning instead of observation.

## The pipeline (all pieces exist, no new tooling)

1. **Run the reference ROM in luna.** krom's ROMs fail header autodetection
   (checksum) — `--force-mapper lorom` (available on `luna frames` and
   `luna state`; NOT on `luna run`).
2. **Capture exactly-consecutive frames from BOTH ROMs**:
   `luna frames -n 3000000 -c 30 --out-dir <dir> [--force-mapper lorom] <rom>`.
3. **Extract the reference's actual DATA, not its vibe**: tables, constants
   and update cadence come from the ASM source verbatim
   (e.g. `grep '^db 1; dw ' WaveHDMA.asm` → the wave samples and their real
   period). Never re-invent values that exist in the original.
4. **Measure the effect on both frame sequences with the same script**
   (numpy + PIL are available). For displacement effects, the working
   estimator: per-scanline horizontal shift between two frames via integer
   SSD correlation on a central slice, then compare displacement-field
   profiles across frame distances (direction = sign of the vertical
   profile shift; speed = shift/Δframes; period = profile autocorrelation;
   amplitude = temporal peak-to-peak per line over ≥ one cycle).
5. **Acceptance = a parity table**, e.g. (hdma_wave_table, 2026-07-12):
   period 25 vs 26 lines, amplitude p2p 19.5 vs 20.0 px, speed ~1 vs 1
   line/frame, direction identical — measured, cited in the example README.

## Traps hit while building this (don't rediscover them)

- **Periodic procedural art breaks the measurement AND the eye**: fixed
  8px stripes make every measured shift ambiguous modulo 8 (the SSD ties
  resolve arbitrarily) and hide horizontal drift bugs. Use aperiodic
  texture (LCG-seeded streak tiles — deterministic, zero assets).
- **Integer-rounded per-frame deltas cumsum to a biased trajectory**
  (true per-frame shift < 1px rounds to 0): for speed, correlate the
  displacement profile between DISTANT frame pairs (model-free), don't
  cumulate consecutive-pair integers.
- `--peek 00:4302` (HDMA A1T0L) reads 0 — DMA registers are not readable
  through luna's peek path; register-level comparison is a dead end,
  measure pixels.
- `luna run` has `--print-fbhash` but no `--force-mapper`; the reference
  ROM therefore can't go through the fbhash path — use `frames`.

## Checklist per port

- [ ] Reference ROM runs in luna (`--force-mapper lorom`), eyeballed.
- [ ] Reference data extracted from the ASM (tables/constants/cadence).
- [ ] Both ROMs captured with `luna frames` (same warm-up, ≥1 effect cycle).
- [ ] Parity table measured with the same script; cited in the README.
- [ ] Side-by-side stills eyeballed (character match, not just numbers).
- [ ] Own art, aperiodic if the effect is a displacement; krom credited.
