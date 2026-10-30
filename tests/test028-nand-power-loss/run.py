#!/usr/bin/env python3
"""test028 automated: 20 random power cuts during plt_write (plt_loop.py), then a clean boot with snapshots;
pass: no lost or corrupt committed file."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
r.off()
csv = os.path.join(r.dir, f"run4_{r.date}.csv")
if os.path.exists(csv):
    os.remove(csv)
t0 = time.time()
p = subprocess.run([sys.executable, os.path.join(r.dir, "plt_loop.py"), "20", csv], capture_output=True, text=True,
                   timeout=1800)
import csv as csvmod   # noqa: E402
rows = list(csvmod.DictReader(open(csv))) if os.path.exists(csv) else []
bad_n = sum(int(x["bad"] or 0) for x in rows)
unparsed = sum(1 for x in rows if not x["checked"])
apps = sorted(float(x["app_init_s"]) for x in rows if x["app_init_s"])
free = [int(x["free_kb"]) for x in rows if x["free_kb"]]
r.boot()
for i in range(8):
    r.sample(f"after cuts {i + 1}")
chk = r.k("plt_check", 60, r"msh />\s*$")
bad = re.search(r"(?i)(corrupt|mismatch|fail)", chk)
# leave /sdcard as found: the files (up to ~7 MB) left 12 MB free and test105's encoder filled it (2026-10-30)
for n in range(16):
    r.k(f"rm /sdcard/plt/f{n:02d}.bin", 3)
r.k("rm /sdcard/plt", 3)
r.set("runtime", f"20 cuts in {time.time() - t0:.0f} s automated (plt_loop.py), no setup ({r.date})")
r.finish(f"{len(rows)} random cuts during plt_write ([{os.path.basename(csv)}]({os.path.basename(csv)})): "
         f"{bad_n} bad files, {unparsed} checks unparsed, "
         f"{sum(int(x['commits_before_cut'] or 0) for x in rows)} commits before the cuts; app init after a cut "
         f"{apps[0]:.3f}-{apps[-1]:.3f} s; free {free[0]} -> {free[-1]} KB; plt_check after the run: "
         f"{'**' + bad.group(1) + '**' if bad else 'clean'}." if rows and apps and free else "plt_loop.py wrote no rows",
         bool(rows) and bad_n == 0 and not bad)
