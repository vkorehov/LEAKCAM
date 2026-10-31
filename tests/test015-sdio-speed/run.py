#!/usr/bin/env python3
"""test015 automated: the SDIO bus alone, K230 MMC0 -> BL616 SDK echo device (pt_sdu), bench from k230_bench.patch
(test image): bind, clock, 2000 x 1514 B, 200 x 1514 B, 50 x 512 B, then 16 x (2000 x 1514 / 2000 x 512) with
samples (leak verdict); BL616 CRC / abort counters before and after."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402

BENCH = r"bl616_bench: \d+ frames[^\n]*\n"
r = Run(__file__)
fails = []


def bench(n, size):
    out = r.k(f"bl616_bench {n} {size}", 30, BENCH)
    m = re.search(r"bl616_bench: (\d+) frames x (\d+) B in (\d+) ms: (\d+) KB/s end to end; bus (\d+) KB in (\d+) ms: "
                  r"(\d+) KB/s;[^\n]*clock (\d+) Hz, (\d+)-bit", out)
    if not m or "STALL" in out or "stopped on an error" in out or int(m.group(1)) != n:
        fails.append(f"bl616_bench {n} {size}: {out.strip()[-200:]!r}")
        return None
    return {"e2e": int(m.group(4)), "bus": int(m.group(7)), "clk": int(m.group(8)), "width": int(m.group(9))}


def sdu():
    m = re.search(r"(\d+) blocks in, (\d+) out, (\d+) KB echoed; CRC errors (\d+), aborts (\d+), other (\d+), resets (\d+)",
                  bl("pt_sdu", 1))
    return tuple(int(x) for x in m.groups()) if m else None


r.boot(after_on=lambda: bl("pt_sdu on", 2))
r.sample("idle")
if "card bound" not in r.k("bl616_rescan", 5, r"bl616_rescan:[^\n]*\n"):
    fails.append("bl616_rescan: no card")
else:
    s0 = sdu()
    m = re.search(r"bl616_clk:[^\r\n]*", r.k("bl616_clk", 2, r"bl616_clk:[^\n]*\n"))
    clk = m.group(0) if m else "bl616_clk: no answer"
    big, mid, small = bench(2000, 1514), bench(200, 1514), bench(50, 512)
    a = r.meter("K230 pushing SDIO frames to the BL616 echo device (bl616_bench 2000 back to back)",
                busy=lambda: r.k("bl616_bench 2000 1514", 30, BENCH))
    r.leak_from = len(r.samples)
    soak = []
    for i in range(16):
        soak += [bench(2000, 1514), bench(2000, 512)]
        r.sample(f"soak {i + 1}")
    s1 = sdu()
    if s0 and s1:
        d = [b - a_ for a_, b in zip(s0, s1)]
        r.notes.append(f"BL616 echo device over the run: {d[0]} blocks in, {d[1]} out, {d[2]} KB; CRC errors {d[3]}, "
                       f"aborts {d[4]}, other {d[5]}, resets {d[6]}")
        if d[3] or d[4] or d[6]:
            fails.append(f"BL616 errors: CRC {d[3]}, aborts {d[4]}, resets {d[6]}")
    else:
        fails.append("pt_sdu counters not read")
    if big:
        r.set("speed", f"bus {big['bus']} KB/s, end to end {big['e2e']} KB/s (2000 x 1514 B; the echo device limits end "
                       f"to end), {big['clk'] // 1000000} MHz {big['width']}-bit: the SDIO 2.0 top, no UHS on the BL616 "
                       f"({r.date})")
        if a:
            secs = 2000 * 1514 / 1024 / big["e2e"]
            r.energy(a, secs, idle_amps=0.220, what="2000 x 1514 B (3 MB)")
    r.set("share", f"n/a: the card binds after the boot (bl616_rescan, the agent's WIFI step) ({r.date})")
    r.set("perday", f"n/a: the bus rate test; the product's Wi-Fi traffic is test100 / ENERGY.txt ({r.date})")
    r.set("features", f"n/a: the bench is test code (k230_bench.patch on the driver, test image only); the driver itself is "
                      f"the product's bl616_nethub ({r.date})")
    r.notes.append(clk)
    for name, x in (("2000 x 1514", big), ("200 x 1514", mid), ("50 x 512", small)):
        if x:
            r.notes.append(f"{name} B: bus {x['bus']} KB/s, end to end {x['e2e']} KB/s")
    ok = [x for x in soak if x]
    r.notes.append(f"soak: {len(ok)} of {len(soak)} runs clean")
r.finish("SDIO bus, K230 -> BL616 echo device: " + ("; ".join(r.notes[-5:]) if not fails else "; ".join(fails)),
         not fails)
