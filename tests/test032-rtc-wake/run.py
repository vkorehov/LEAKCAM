#!/usr/bin/env python3
"""test032 automated: BL616 RTC wake (pt_sleep <s> nousb: HBN, the console leaves and comes back) 3 x 10 s and 2 x
600 s, timed on the host from the command to the console's return; the difference of the two lengths cancels the
re-enumeration time: (T600 - T10 - 590) / 590 = the RTC (Y3) error."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401
from board import BL_PORT   # noqa: E402

r = Run(__file__)
r.off()


def sleep_once(s):
    t0 = time.time()
    bl(f"pt_sleep {s} nousb", 0.5)
    time.sleep(2)
    while os.path.exists(BL_PORT) and time.time() - t0 < 5:
        time.sleep(0.05)                      # the console leaves
    while not os.path.exists(BL_PORT):
        if time.time() - t0 > s + 60:
            return None
        time.sleep(0.02)
    return time.time() - t0


LONG = 600
t = {10: [], LONG: []}
for s in (10, 10, 10, LONG, LONG):
    d = sleep_once(s)
    if d is None:
        r.obstacle(f"no wake after pt_sleep {s}")
    else:
        t[s].append(d)
    time.sleep(3)
    r.sample(f"after {s} s")
ok = len(t[10]) == 3 and len(t[LONG]) == 2
if ok:
    a, b = min(t[10]), min(t[LONG])     # enumeration only adds delay: the shortest is the least disturbed
    ppm = ((b - a) - (LONG - 10)) / (LONG - 10) * 1e6
    over = a - 10
    r.set(r"wake time error", f"Y3 {ppm:+.0f} ppm (host-timed, shortest of 3 x 10 s vs of 2 x 600 s, +-35 ppm; console return {over:.2f} s "
          f"after the due time incl. USB enumeration; spread {max(t[LONG]) - min(t[LONG]):.2f} s) ({r.date})")
r.set("boot", f"n/a: BL616-only; RTC wake -> main() 12-16 ms (test021) ({r.date})")
r.set("share", f"n/a: BL616-only ({r.date})")
r.set("voltage", f"n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) ({r.date})")
r.set("energy", f"n/a: the sleep itself is test031 (35 uA); the wake is below the USB meter's resolution ({r.date})")
r.set("perday", f"96 RTC wakes a day (15 min): inside the test031 sleep budget ({r.date})")
r.finish(f"RTC wakes: 10 s -> {', '.join(f'{x:.2f}' for x in t[10])} s; {LONG} s -> {', '.join(f'{x:.2f}' for x in t[LONG])} s "
         "(host, command -> console back).", ok, switch_off=False)
