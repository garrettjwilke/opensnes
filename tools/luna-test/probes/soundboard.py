"""Probe: soundboard — the audio v2 engine driven by pad input.

Asserts on examples/audio/soundboard:
1. boot: play_count == 0 (nothing pressed, engine idle);
2. press A: play_count >= 1 and last_voice == 0 (first round-robin
   voice) — proving audioInit + audioLoadSample x4 + audioPlaySampleEx
   all completed from plain C in a real example ROM.

Single-button script only (luna#126: checkpoint replay re-fires; a
repeated A press converges — extra plays bump play_count but the
asserts are >= / final-state safe).
"""
from __future__ import annotations

import sys

from lib import find_luna, rom_path, peek, A

ROM_REL = "audio/soundboard/soundboard.sfc"
STEPS = 10_000_000  # audioInit + 4 loads (~8.4 KB streamed) + the press


def run() -> tuple[bool, str]:
    luna = find_luna()
    rom = rom_path(ROM_REL)

    boot = peek(luna, rom, STEPS, "play_count", 2, None)
    boot_count = boot[0] | (boot[1] << 8)

    after = peek(luna, rom, STEPS, "play_count", 2, f"120:{A:#x},123:0")
    after_count = after[0] | (after[1] << 8)
    voice = peek(luna, rom, STEPS, "last_voice", 1, f"120:{A:#x},123:0")[0]

    checks = [
        (boot_count == 0, f"boot plays={boot_count} (want 0)"),
        (after_count >= 1, f"after A plays={after_count} (want >=1)"),
        (voice == 0, f"last_voice={voice} (want 0)"),
    ]
    bad = [m for ok, m in checks if not ok]
    if bad:
        return False, "FAILED: " + "; ".join(bad)
    return True, "; ".join(m for _, m in checks)


if __name__ == "__main__":
    ok, msg = run()
    print(("PASS " if ok else "FAIL ") + "soundboard: " + msg)
    sys.exit(0 if ok else 1)
