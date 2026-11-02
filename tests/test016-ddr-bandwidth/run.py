#!/usr/bin/env python3
"""test016 automated: ddr_bench 3072 (2 x 3 MB, beyond the caches), 8 runs with snapshots (leak verdict)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
lines = []
dur = []
for i in range(8):
    t0 = time.time()
    out = r.k("ddr_bench 3072", 30, r"msh />\s*$")
    dur.append(time.time() - t0)
    lines += [l.strip() for l in out.splitlines() if "GB/s" in l or "MB/s" in l]
    r.sample(f"run {i + 1}")
    if i == 0:
        a = r.meter("K230 running ddr_bench 3072 over and over", busy=lambda: r.k("ddr_bench 3072", 30, r"msh />\s*$"))
dur.sort()
r.energy(a, dur[len(dur) // 2], idle_amps=0.220, what="ddr_bench 3072 (host-timed)")
r.finish("8 x `ddr_bench 3072`; last run:\n\n" + "\n".join(f"    {l}" for l in lines[-4:]), bool(lines))
