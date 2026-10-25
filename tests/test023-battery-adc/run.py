#!/usr/bin/env python3
"""test023 automated: VBAT on the K230 ADC (64 reads per sample, test023 calibration) at idle and under memtest
load, 8 snapshots; on USB it reads the charger rail."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
for i in range(4):
    r.sample(f"idle {i + 1}")
r.k("/sdcard/app/memtest 0 24 &", 2)
for i in range(4):
    time.sleep(5)
    r.sample(f"memtest {i + 1}")
r.k("killall memtest", 3)
v = [s["vbat"] for s in r.samples if s.get("vbat")]
r.finish(f"VBAT {min(v)}-{max(v)} mV over {len(v)} samples (idle, then memtest), spread {max(v) - min(v)} mV.",
         bool(v) and max(v) - min(v) < 150)
