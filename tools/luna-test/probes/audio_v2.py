"""Probe: audio v2 driver — DSP effects of the C command layer.

The libtest fixture (devtools/libtests) calls audioInit() then a batch
of setters with known vectors (see its main.c). The WRAM-visible
results are asserted by test_libtest.py; THIS probe asserts the other
half of the contract: the driver actually poked the S-DSP. We spc-dump
the libtest ROM after the fixture ran and check the DSP register file:

  MVOL L/R = 100            (audioSetVolume(100))
  V2 VOLL/VOLR = 80/40      (audioSetVoiceVolume(2, 80, 40))
  V3 PITCH = $1234          (audioSetVoicePitch(3, 0x1234))
  V1 ADSR = $FF/$E8         (audioSetADSR(1, 15,7,7,8) — the packing)
  V4 ADSR1 = 0, GAIN = $5A  (audioSetGain zeroes ADSR1: hot-swap hygiene)
  DIR = $0A                 (driver init: directory page $0A00)

plus the driver image itself at ARAM $0200 (upload integrity).
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from lib import find_luna

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
LIBTEST_ROM = REPO / "devtools" / "libtests" / "libtest.sfc"
DRIVER_BIN = REPO / "lib" / "source" / "audio_driver.spc700.bin"
STEPS = 3_000_000  # matches test_libtest.py: past audioInit + setters


def run() -> tuple[bool, str]:
    luna = find_luna()
    if not LIBTEST_ROM.is_file():
        return False, f"libtest.sfc not built ({LIBTEST_ROM})"

    with tempfile.NamedTemporaryFile(suffix=".spc", delete=False) as tf:
        spc_path = Path(tf.name)
    try:
        proc = subprocess.run(
            [luna, "spc-dump", "-n", str(STEPS), "-o", str(spc_path),
             str(LIBTEST_ROM)],
            capture_output=True, text=True, timeout=300,
        )
        if proc.returncode != 0:
            return False, f"spc-dump failed: {proc.stderr.strip()[:150]}"
        spc = spc_path.read_bytes()
    finally:
        spc_path.unlink(missing_ok=True)

    aram = spc[0x100:0x10100]
    dsp = spc[0x10100:0x10180]

    checks = [
        (dsp[0x0C] == 100 and dsp[0x1C] == 100,
         f"MVOL={dsp[0x0C]}/{dsp[0x1C]} (want 100/100)"),
        (dsp[0x20] == 80 and dsp[0x21] == 40,
         f"V2VOL={dsp[0x20]}/{dsp[0x21]} (want 80/40)"),
        (dsp[0x32] == 0x34 and dsp[0x33] == 0x12,
         f"V3PITCH={dsp[0x33]:02X}{dsp[0x32]:02X} (want 1234)"),
        (dsp[0x15] == 0xFF and dsp[0x16] == 0xE8,
         f"V1ADSR={dsp[0x15]:02X}/{dsp[0x16]:02X} (want FF/E8)"),
        (dsp[0x45] == 0x00 and dsp[0x47] == 0x5A,
         f"V4 ADSR1={dsp[0x45]:02X} GAIN={dsp[0x47]:02X} (want 00/5A)"),
        (dsp[0x5D] == 0x0A, f"DIR={dsp[0x5D]:02X} (want 0A)"),
    ]

    if DRIVER_BIN.is_file():
        drv = DRIVER_BIN.read_bytes()
        checks.append(
            (aram[0x200:0x200 + len(drv)] == drv,
             f"driver image at $0200 ({len(drv)} bytes)"))
    else:
        checks.append((False, f"driver bin missing ({DRIVER_BIN}) — build the lib"))

    bad = [m for ok, m in checks if not ok]
    if bad:
        return False, "FAILED: " + "; ".join(bad)
    return True, "; ".join(m for _, m in checks)


if __name__ == "__main__":
    ok, msg = run()
    print(("PASS " if ok else "FAIL ") + "audio_v2: " + msg)
    sys.exit(0 if ok else 1)
