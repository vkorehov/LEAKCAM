#!/usr/bin/env python3
"""test016 automated: ddr_bench 3072 (2 x 3 MB, beyond the caches), 8 runs with snapshots (leak verdict)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
lines = []
for i in range(8):
    out = r.k("ddr_bench 3072", 30, r"msh />\s*$")
    lines += [l.strip() for l in out.splitlines() if "GB/s" in l or "MB/s" in l]
    r.sample(f"run {i + 1}")
    if i == 0:
        r.meter("K230 running ddr_bench 3072 over and over", busy=lambda: r.k("ddr_bench 3072", 30, r"msh />\s*$"))
r.finish("8 x `ddr_bench 3072`; last run:\n\n" + "\n".join(f"    {l}" for l in lines[-4:]), bool(lines))
