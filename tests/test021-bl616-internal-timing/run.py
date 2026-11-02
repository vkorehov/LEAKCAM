#!/usr/bin/env python3
"""test021 automated: 8 RTC sleep/wake cycles of the BL616 (pt_sleep 3 nousb: HBN, the USB console leaves and
comes back), after each: pt_time (wake -> main, core timer at main, K230_PWR -> 3V3 up, AHT20 trigger -> data)."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO   # noqa: E402,F401
from board import BL_PORT   # noqa: E402

r = Run(__file__)
r.off()
wake, mainu, rail, aht = [], [], [], []
for i in range(8):
    bl("pt_sleep 3 nousb", 1)
    time.sleep(5)
    for _ in range(30):
        if os.path.exists(BL_PORT):
            break
        time.sleep(1)
    time.sleep(2)
    bl("pt_off", 2)
    out = bl("pt_time", 4, r"AHT20 trigger[^\n]*\n")
    for pat, dst in ((r"wake -> main: (\d+) us", wake), (r"core timer at main\(\): (\d+) us", mainu),
                     (r"K230_PWR -> 3V3 up: (\d+) us", rail), (r"AHT20 trigger -> data: \d+ \.\. (\d+) us", aht)):
        m = re.search(pat, out)
        if m:
            dst.append(int(m.group(1)) / 1000)
    bl("pt_off", 2)
    r.sample(f"wake {i + 1}")
f = lambda v: f"{min(v):.1f} / {sorted(v)[len(v) // 2]:.1f} / {max(v):.1f} ms" if v else "none"
steps = (f"RTC wake -> main() {f(wake)}; core reset -> main() {f(mainu)}; K230_PWR -> 3V3 {f(rail)}; AHT20 "
         f"trigger -> data {f(aht)} (min / median / max of {len(wake)} wakes)")
r.set(r"BL616 step times", steps + f" ({r.date})")
r.set("boot", f"n/a: BL616-only test; its own boot (RTC wake -> main) {f(wake)} ({r.date})")
r.set("share", f"wake -> main() is the BL616's whole share of a wake: {f(wake)} ({r.date})")
r.set("voltage", f"n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) ({r.date})")
r.set("energy", f"n/a: the BL616's wake (~15 ms) is below what the USB meter resolves; on the cell: test104 ({r.date})")
r.set("perday", f"n/a: as energy; the wake cost sits in the sleep floor of test031 ({r.date})")
r.finish(steps + ".", len(wake) >= 7, switch_off=False)
