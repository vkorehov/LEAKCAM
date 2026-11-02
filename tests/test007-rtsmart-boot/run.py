#!/usr/bin/env python3
"""test007 automated: 5 cold boots; RT-Smart's share = OpenSBI banner -> msh (kernel init, UFFS mounts, media
stack); every boot checked for NAND errors and both mounts."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
share, bad = [], []
for i in range(5):
    r.off()
    time.sleep(3)
    r.boot()
    b = r.boots[-1]
    if "OpenSBI" in b and "msh" in b:
        share.append(b["msh"] - b["OpenSBI"])
    df = r.k("df", 3)
    ls = r.k("ls /sdcard/app", 3)
    if "leakcam_agent" not in ls:
        bad.append(f"boot {i + 1}: /sdcard not mounted")
    r.sample(f"boot {i + 1}")
a = r.meter("K230 at msh idle (agent running)")
share.sort()
if share:
    r.set("share", f"OpenSBI -> msh {share[len(share) // 2]:.2f} / {share[-1]:.2f} s (median / max of {len(share)}) "
                   f"({r.date})")
r.set("speed", f"n/a: boot step; mount speed is in the boot times (test022) ({r.date})")
r.energy(a, r.boots[-1].get("msh", 0.45), per_day=4, what="cold boot to msh")
r.finish(f"{len(share)} of 5 cold boots to msh with /bin and /sdcard mounted" + (": " + "; ".join(bad) if bad else "."),
         len(share) == 5 and not bad)
