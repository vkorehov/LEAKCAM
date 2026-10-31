#!/usr/bin/env python3
"""test026 automated: die (chip_temp) and board (AHT20) every 10 s: 2 min idle, then 3 min memtest (CPU + DDR),
2 min cooling, then 3 min each of the KPU change net in a loop, the encoder 2 x 15 fps, KPU + encoder together,
and 2 min cooling (the KPU / encoder loads added 2026-10-31)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
for i in range(12):
    r.sample(f"idle {i * 10} s")
    time.sleep(8)
r.k("/sdcard/app/memtest 0 24 &", 2)
load = r.meter("K230 running memtest (CPU + DDR)")
for i in range(18):
    r.sample(f"memtest {i * 10} s")
    time.sleep(8)
r.k("killall memtest", 3)
for i in range(12):
    r.sample(f"cool {i * 10} s")
    time.sleep(8)


def phase(name, start, stop, n=18):
    out = "".join(r.k(c, 3) for c in start) + r.k("", 3)
    if re.search(r"(?i)failed|not found|out of memory", re.sub(r".*not use fast load.*", "", out)):
        r.obstacle(f"{name}: did not start: {out.strip()[-120:]}")
        return []
    got = []
    for i in range(n):
        got.append(r.sample(f"{name} {i * 10} s").get("die"))
        time.sleep(8)
    for c in stop:
        r.k(c, 3)
    return [d for d in got if d is not None]


KPU = "/sdcard/app/leakcam_kputest 200000000 &"
ENC = "/sdcard/app/venctest 2 200 15 &"
kpu = phase("KPU", [KPU], ["killall leakcam_kputest"])
enc = phase("encoder", [ENC], ["killall venctest"])
both = phase("KPU + encoder", [KPU, ENC], ["killall leakcam_kputest", "killall venctest"])
for i in range(12):
    r.sample(f"cool 2 {i * 10} s")
    time.sleep(8)
for name, d in (("KPU", kpu), ("encoder", enc), ("KPU + encoder", both)):
    if d:
        r.notes.append(f"{name}: die {d[0]:.1f} -> {max(d):.1f} C peak over 3 min")
die = [s["die"] for s in r.samples if s.get("die")]
idle = sum(die[:12]) / 12 if len(die) >= 12 else None
r.set("speed", f"n/a: temperature test; rise idle -> memtest {max(die) - idle:.1f} C ({r.date})" if idle else None)
r.finish(f"die idle {idle:.1f} C, memtest peak {max(die):.1f} C, end {die[-1]:.1f} C." if idle else "no die readings",
         bool(die) and max(die) < 100)
