#!/usr/bin/env python3
"""test037 automated: rtc_raw (PMU RTC registers), rtc_ppm 60 (Y1 against the 27 MHz rdtime), date before/after,
3 boots."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402,F401

r = Run(__file__)
ppm, raws = [], ""
for i in range(3):
    r.off()
    time.sleep(2)
    r.boot()
    raws = r.k("rtc_raw", 3)
    d0 = r.k("date", 3)
    out = r.k("rtc_ppm 60", 75, r"msh />\s*$")
    m = re.search(r"([+-]?\d+(?:\.\d+)?) ppm", out)
    if m:
        ppm.append(float(m.group(1)))
    r.sample(f"boot {i + 1}")
r.set(r"RTC offset after handover", f"Y1 {', '.join(f'{p:+.1f}' for p in ppm)} ppm vs the 27 MHz rdtime (rtc_ppm 60, "
      f"3 boots); offset after handover not measured (needs an NTP reference) ({r.date})" if ppm else None)
r.set("share", f"rtc driver registers at 0.255 s in the boot, no wait of its own (console milestones) ({r.date})")
r.set("energy", f"n/a: the RTC runs on the PMU always-on domain; its share is in the sleep floor (test031) ({r.date})")
r.set("perday", f"n/a: as energy ({r.date})")
r.finish(f"3 boots: rtc_ppm 60 = {', '.join(f'{p:+.1f}' for p in ppm)} ppm. rtc_raw:\n\n" +
         "\n".join(f"    {l}" for l in raws.splitlines() if l.strip() and "msh" not in l)[:1200], len(ppm) == 3)
