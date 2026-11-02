#!/usr/bin/env python3
"""test013 automated (test image): leakcam_kputest 50 (the factory int8 change net on the KPU, synthetic pairs:
same / light / change), 8 runs with snapshots (leak verdict)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
idle = r.meter("K230 at msh idle (agent running)", force=True)
med, verdicts, dist = [], [], ""
for i in range(8):
    out = r.k("/sdcard/app/leakcam_kputest 50", 60, r"kputest: (PASS|FAIL)")
    m = re.search(r"min (\d+) / median (\d+) / max (\d+) us", out)
    if m:
        med.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    v = re.search(r"kputest: (PASS|FAIL)", out)
    verdicts.append(v.group(1) if v else "none")
    d = re.search(r"distance [^\n]*", out)
    dist = d.group(0) if d else dist
    r.sample(f"run {i + 1}")
busy = r.meter("K230 running leakcam_kputest 50 over and over", force=True,
               busy=lambda: r.k("/sdcard/app/leakcam_kputest 50", 60, r"kputest: (PASS|FAIL)"))
ok = med and all(v == "PASS" for v in verdicts)
if med:
    r.set("speed", f"one distance (2 inferences + cosine) {min(x[0] for x in med) / 1000:.1f} / "
          f"{sorted(x[1] for x in med)[len(med) // 2] / 1000:.1f} / {max(x[2] for x in med) / 1000:.1f} ms (min / median "
          f"/ max, 8 x 50) ({r.date})")
    r.energy(busy, sorted(x[1] for x in med)[len(med) // 2] / 1e6, per_day=4 * 2, idle_amps=idle,
             what="change-net distance")
r.set("share", f"n/a: the model loads in the program (kputest prints the load time) ({r.date})")
r.finish(f"8 x leakcam_kputest 50: {', '.join(verdicts)}; {dist}.", bool(ok))
