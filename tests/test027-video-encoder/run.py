#!/usr/bin/env python3
"""test027 automated (test image): venctest 2 channels at 15 fps for 20 s (the product setting) and 1 channel at the
encoder's top for 15 s; die temperature after each."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
res = []
for args, name in (("2 20 15", "2 x 15 fps"), ("1 15 0", "1 channel max")):
    out = r.k(f"/sdcard/app/venctest {args}", 60, r"venctest: done")
    fps = re.findall(r"ch(\d): \d+ in, \d+ out in [\d.]+ s = ([\d.]+) fps, (\d+) kbit/s", out)
    res.append((name, fps, "venctest: done" in out))
    r.sample(name)
ok = all(done and fps for _, fps, done in res)
r.set("share", f"n/a: the encoder starts in the program ({r.date})")
r.set("perday", f"4 alarm clips a day x 20 s of 2 x 15 fps: see ENERGY.txt (the encoder adds ~15 mA, 2026-10-21) "
                f"({r.date})")
r.finish("; ".join(f"{n}: " + ", ".join(f"ch{c} {f} fps {k} kbit/s" for c, f, k in fps) + ("" if d else " (NOT done)")
                   for n, fps, d in res) + ".", ok)
