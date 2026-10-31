#!/usr/bin/env python3
"""test039 automated: the K230 console on /dev/ttyACM1 through the BL616: 4 boots to msh, 250 command round
trips each (version), 5 s per answer: latency, late (> 2 s) and lost answers with their index (2026-10-25: 1-2 of 400 lost)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402

r = Run(__file__)
lat, lost, lost_raw, counts = [], 0, [], []


def bl_console():
    """BL616: (bytes from the host, sent on to the K230, puts failed) since its boot"""
    m = re.search(r"K230 console: (\d+) bytes from the host, (\d+) sent on to the K230, (\d+) puts failed",
                  bl("pt_status", 2))
    return tuple(int(x) for x in m.groups()) if m else None


def k_link():
    """K230: uart1 bytes in, text bytes dropped at the console ring, uart1 overruns, bytes dropped and FIFO resets
    (link_stat)"""
    out = r.k("link_stat", 2, r"irqs[^\n]*\n")
    a = re.search(r"link_stat: rx (\d+) text (\d+) text_drop (\d+)", out)
    b = re.search(r"overrun (\d+), bytes dropped (\d+)", out)
    c = re.search(r"uart1 resets (\d+)", out)
    return ((int(a.group(1)), int(a.group(3)), int(b.group(1)), int(b.group(2)), int(c.group(1)))
            if a and b and c else None)

for i in range(4):
    r.off()
    time.sleep(2)
    r.boot()
    b0, k0 = bl_console(), k_link()
    for j in range(250):
        t0 = time.time()
        out = r.k("version", 5, r"build[^\n]*\n")
        if "build" in out:
            lat.append((time.time() - t0) * 1000)
        else:
            lost += 1
            lost_raw.append(f"boot {i + 1} command {j + 1} ({time.time() - r.t0:.0f} s into the run): {out[-200:]!r}")
    if i == 0:
        r.meter("K230 answering console commands over the link (version, back to back)",
                busy=lambda: r.k("version", 5, r"build[^\n]*\n"))
    b1, k1 = bl_console(), k_link()
    if b0 and b1 and k0 and k1:
        # host bytes typed (the harness sends "version\r\n", 9 bytes, plus the two link_stat lines) against what
        # each hop counted: a loss between BL616 and K230 shows as sent > K230 rx or as overruns (test039 Open)
        c = {"boot": i + 1, "host_to_bl": b1[0] - b0[0], "bl_to_k230": b1[1] - b0[1], "bl_put_fail": b1[2] - b0[2],
             "k230_rx": k1[0] - k0[0], "k230_text_drop": k1[1] - k0[1], "k230_overrun": k1[2] - k0[2],
             "k230_bytes_dropped": k1[3] - k0[3], "k230_uart_resets": k1[4] - k0[4]}
        counts.append(c)
        r.notes.append(f"counters boot {i + 1}: " + ", ".join(f"{k} {v}" for k, v in c.items() if k != "boot"))
    r.sample(f"boot {i + 1}")
lat.sort()
r.set("share", f"link_mux registers at 0.27 s with the other drivers; the console is up from the SPL banner ({r.date})")
r.set("perday", f"n/a: the console is a bring-up path, not used in a product session ({r.date})")
for c in counts:
    print("counters", c)
for x in lost_raw:
    r.obstacle(f"lost `version` answer, raw: {x}")
r.finish(f"4 boots, {len(lat)} of {len(lat) + lost} `version` round trips answered; host-timed command -> answer "
         f"{lat[0]:.0f} / {lat[len(lat) // 2]:.0f} / {lat[-1]:.0f} ms (min / median / max); {sum(1 for x in lat if x > 2000)} late (> 2 s)." if lat else "no answers",
         lost == 0)
