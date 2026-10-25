#!/usr/bin/env python3
"""test003 automated: rails on/off 8 times (3V3 ramp from pt_on, all 5 SDIO pull-ups up, 0 after pt_off), then
USB boot (BOOT1 high): the K230 ROM enumerates as 29f1:0230 on USB-C 1."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
ramp, fails = [], []
for i in range(8):
    r.off()
    time.sleep(1)
    out = bl("pt_on", 2)
    m = re.search(r"3V3 up ([\d.]+) ms", out)
    if m:
        ramp.append(float(m.group(1)))
    if "5 of 5" not in bl("pt_status", 2):
        fails.append(f"cycle {i + 1}: 3V3 not up")
    r.sample(f"cycle {i + 1}")
    r.off()
    if "0 of 5 SDIO pull-ups high" not in bl("pt_status", 2):
        fails.append(f"cycle {i + 1}: 3V3 not off")
r.boot(boot1="high", until=b"zzz", secs=3, expect_msh=False)
rom = None
for _ in range(20):
    if "29f1:0230" in subprocess.run(["lsusb"], capture_output=True, text=True).stdout:
        rom = True
        break
    time.sleep(0.5)
if not rom:
    fails.append("K230 ROM did not enumerate (29f1:0230) in USB boot")
r.off()
bl("pt_boot1 low")
r.set("boot", f"n/a: rails test; reset release -> ROM on USB {'seen' if rom else 'not seen'} within 10 s ({r.date})")
r.set("share", f"K230_PWR -> 3V3 up {min(ramp):.2f}-{max(ramp):.2f} ms (pt_on, 8 cycles) ({r.date})" if ramp else None)
r.set("speed", f"n/a: rails test ({r.date})")
a = r.meter("K230 rails on, held in reset", force=True)
r.energy(a, 0.002, what="rail ramp") if a else None
r.set("perday", f"n/a: the rails come up once per session; the session energy is test102 ({r.date})")
r.finish(f"8 rail cycles: 3V3 up in {min(ramp):.2f}-{max(ramp):.2f} ms, off after pt_off every time; USB boot ROM "
         f"{'enumerated (29f1:0230)' if rom else 'NOT seen'}." if ramp else "no 3V3 ramp reported", not fails)
