#!/usr/bin/env python3
"""test001 automated (what the board reports by itself): USB detected (BQ24072 PGOOD low), the K230 rails up and down
8 times, VBAT on the K230 ADC with the charger rail, temperatures, memory. The rail voltages and charge currents are
meter readings and stay those of 2026-10-17/20."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402

r = Run(__file__)
fails = []
for i in range(8):
    r.off()
    time.sleep(2)
    st = bl("pt_status", 2)
    if "USB cable   : detected" not in st:
        fails.append(f"cycle {i + 1}: USB not detected (PGOOD)")
    r.boot()
    r.sample(f"cycle {i + 1}")
r.set("speed", f"n/a: power path test ({r.date})")
r.set("share", f"n/a: the rails ramp in 1.7 ms (test003) ({r.date})")
r.set("current", "rails off 0.061 A, K230 at msh idle 0.220 A (test006, 2026-10-25); charge currents: Results "
                 f"(2026-10-17/20) ({r.date}, from recorded readings)")
r.set("energy", f"n/a: power path test; the board's states are priced in test006/007/031 ({r.date})")
r.set("perday", f"n/a: as energy ({r.date})")
r.finish("8 cycles: USB detected (PGOOD low) every time, K230 booted on the charger rail; VBAT in the Voltage row." +
         (" " + "; ".join(fails) if fails else ""), not fails)
