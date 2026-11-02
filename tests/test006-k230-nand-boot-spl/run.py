#!/usr/bin/env python3
"""test006 automated: 5 cold boots from NAND; the SPL's share = reset release -> OpenSBI banner (ROM loads SPL,
SPL trains DDR and loads OpenSBI + the kernel)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.meter("K230 rails off, BL616 test firmware awake on USB")
spl = []
for i in range(5):
    r.off()
    time.sleep(3)
    r.boot()
    b = r.boots[-1]
    if "OpenSBI" in b:
        spl.append(b["OpenSBI"])
    r.sample(f"boot {i + 1}")
a = r.meter("K230 at msh idle (agent running)", force=True)
spl.sort()
if spl:
    r.energy(a, spl[len(spl) // 2], per_day=4, what="SPL stage (reset -> OpenSBI)")
ok = len(spl) == 5
if spl:
    r.set("share", f"reset release -> OpenSBI {spl[len(spl) // 2]:.2f} / {spl[-1]:.2f} s (median / max of {len(spl)}; "
                   f"ROM + SPL + DDR training + kernel load) ({r.date})")
r.set("speed", f"n/a: boot step; the NAND read speed in the boot is test017 ({r.date})")
r.finish(f"{len(spl)} of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.", ok)
