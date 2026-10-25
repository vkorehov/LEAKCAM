#!/usr/bin/env python3
"""test025 automated, short: memtest 10 passes (24 MB kept free) with die / board temperature and VBAT every 30 s; the
8 h soak of 2026-10-24 stays the stress result."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.sample("idle")
r.k("/sdcard/app/memtest 10 24 &", 2)
t0 = time.time()
start = len(r.console)
while time.time() - t0 < 1800 and "memtest: done" not in r.console[start:]:
    time.sleep(25)
    r.k(None, 1)
    r.sample(f"memtest {time.time() - t0:.0f} s")
out = r.console[start:]
passes = re.findall(r"pass (\d+): .*errors (\d+) \(total (\d+)\)", out)
done = re.search(r"memtest: done, (\d+) errors", out)
bad = (done and int(done.group(1))) or any(int(e) for _, e, _ in passes) or "ERROR pass" in out
r.k("killall memtest", 3)
r.finish(f"memtest: {len(passes)} pass lines seen, " + (f"done with {done.group(1)} errors" if done else "**no done line**") +
         f" in {time.time() - t0:.0f} s.", bool(done) and not bad)
