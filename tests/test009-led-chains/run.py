#!/usr/bin/env python3
"""test009 automated (no meter: the currents of 2026-10-25 stay): both chains stepped 0 / 25 / 50 / 100 %, then both
100 % for 2 min; die and board temperature after every step; LEDs off at the end whatever happens."""
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO, KDIMG   # noqa: E402,F401

r = Run(__file__)
r.boot()
P = 40000


def pwm(ch, pct):
    if pct:
        r.k(f"pwm_set pwm {ch} {P} {P * pct // 100}", 1)
        r.k(f"pwm_enable pwm {ch}", 1)
    else:
        r.k(f"pwm_disable pwm {ch}", 1)


try:
    pwm(0, 0); pwm(1, 0)
    r.sample("off")
    for ch, name in ((1, "white"), (0, "IR")):
        for p in (25, 50, 100):
            pwm(ch, p)
            time.sleep(5)
            r.sample(f"{name} {p} %")
        pwm(ch, 0)
    pwm(0, 100); pwm(1, 100)
    for i in range(12):
        time.sleep(8)
        r.sample(f"both 100 % {i * 10} s")
finally:
    try:
        pwm(0, 0); pwm(1, 0)
    except OSError:
        pass
r.sample("off again")
r.set("share", f"n/a: the LEDs start in the program, not in the boot ({r.date})")
die = [s["die"] for s in r.samples if s.get("die")]
r.finish(f"white and IR stepped 25 / 50 / 100 %, both 100 % for 2 min: die {die[0]:.1f} -> {max(die):.1f} C." if die
         else "no die readings", bool(die))
