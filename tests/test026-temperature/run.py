#!/usr/bin/env python3
"""test026 automated: die (chip_temp) and board (AHT20) every 10 s: 2 min idle, then 3 min memtest (CPU + DDR),
then 2 min cooling."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
for i in range(12):
    r.sample(f"idle {i * 10} s")
    time.sleep(8)
r.k("/sdcard/app/memtest 0 24 &", 2)
load = r.meter("K230 running memtest (CPU + DDR)")
for i in range(18):
    r.sample(f"memtest {i * 10} s")
    time.sleep(8)
r.k("killall memtest", 3)
for i in range(12):
    r.sample(f"cool {i * 10} s")
    time.sleep(8)
die = [s["die"] for s in r.samples if s.get("die")]
idle = sum(die[:12]) / 12 if len(die) >= 12 else None
r.set("speed", f"n/a: temperature test; rise idle -> memtest {max(die) - idle:.1f} C ({r.date})" if idle else None)
r.finish(f"die idle {idle:.1f} C, memtest peak {max(die):.1f} C, end {die[-1]:.1f} C." if idle else "no die readings",
         bool(die) and max(die) < 100)
