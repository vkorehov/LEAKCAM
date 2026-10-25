#!/usr/bin/env python3
"""test039 automated: the K230 console on /dev/ttyACM1 through the BL616: 4 boots to msh, 250 command round
trips each (version), 5 s per answer: latency, late (> 2 s) and lost answers with their index (2026-10-25: 1-2 of 400 lost)."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
lat, lost, lost_raw = [], 0, []
for i in range(4):
    r.off()
    time.sleep(2)
    r.boot()
    for j in range(250):
        t0 = time.time()
        out = r.k("version", 5, r"build[^\n]*\n")
        if "build" in out:
            lat.append((time.time() - t0) * 1000)
        else:
            lost += 1
            lost_raw.append(f"boot {i + 1} command {j + 1} ({time.time() - r.t0:.0f} s into the run): {out[-200:]!r}")
    r.sample(f"boot {i + 1}")
lat.sort()
r.set("share", f"link_mux registers at 0.27 s with the other drivers; the console is up from the SPL banner ({r.date})")
r.set("energy", f"n/a: the console bridge's own current is below the meter's step next to the K230 ({r.date})")
r.set("perday", f"n/a: the console is a bring-up path, not used in a product session ({r.date})")
for x in lost_raw:
    r.obstacle(f"lost `version` answer, raw: {x}")
r.finish(f"4 boots, {len(lat)} of {len(lat) + lost} `version` round trips answered; host-timed command -> answer "
         f"{lat[0]:.0f} / {lat[len(lat) // 2]:.0f} / {lat[-1]:.0f} ms (min / median / max); {sum(1 for x in lat if x > 2000)} late (> 2 s)." if lat else "no answers",
         lost == 0)
