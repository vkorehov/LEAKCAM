#!/usr/bin/env python3
"""test017 automated: nand_bench read 64 at the configured 50 MHz / delay 5, 8 runs (checksums must agree), snapshots."""
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
sums, lines, busy = set(), [], None
for i in range(8):
    out = r.k("nand_bench read 64", 30, r"msh />\s*$")
    lines += [l.strip() for l in out.splitlines() if "checksum" in l or "MB/s" in l]
    sums.update(re.findall(r"checksum ([0-9a-fA-F]+)", out))
    r.sample(f"run {i + 1}")
    if i == 0:
        busy = r.meter("K230 running nand_bench read 64 over and over", force=True,
                       busy=lambda: r.k("nand_bench read 64", 30, r"msh />\s*$"))
mb = [float(x) for x in re.findall(r"([\d.]+) MB/s", "\n".join(lines))]
if mb:
    r.energy(busy, 64 * 128 * 1024 / (max(mb) * 1e6), per_day=4, idle_amps=idle, what="8 MB raw read (64 blocks)")
ok = bool(lines) and len(sums) == 1
r.set("speed", f"raw page read {max(mb):.1f} MB/s at 50 MHz (best of 8) ({r.date})" if mb else None)
r.finish("8 x `nand_bench read 64`, checksums " + (f"equal ({next(iter(sums))})" if len(sums) == 1 else f"DIFFER: {sums}") +
         "; last:\n\n" + "\n".join(f"    {l}" for l in lines[-3:]), ok)
