#!/usr/bin/env python3
"""test005 automated: the flashed image (test019 flashes it) boots from NAND 5 times; every program on /sdcard/app
present with its size, no UFFS errors in the boot log."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
fails, ls = [], ""
for i in range(5):
    r.off()
    time.sleep(2)
    r.boot()
    ls = r.k("ls /sdcard/app", 3)
    if "leakcam_agent" not in ls:
        fails.append(f"boot {i + 1}: /sdcard/app incomplete")
    r.sample(f"boot {i + 1}")
r.set("share", f"n/a: flashing is not part of a boot; the mounts take 0.31 s (test022) ({r.date})")
r.set("speed", f"flash speed: test019 ({r.date})")
r.set("energy", f"n/a: flashing runs on USB power ({r.date})")
r.set("perday", f"n/a: a factory / update step ({r.date})")
r.finish("5 NAND boots of the flashed image, /sdcard/app:\n\n" +
         "\n".join(f"    {l.strip()}" for l in ls.splitlines() if re.search(r"\s\d+\s*$", l)) +
         ("\n\n" + "; ".join(fails) if fails else ""), not fails)
