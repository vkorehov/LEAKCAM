#!/usr/bin/env python3
"""test018 automated (test image): pt_uart echo, then uart_bench 1024 (16 B latency x 200, 1 MB checked), 8 runs, leakcam_agent stopped first;
the BL616 sends the command (pt_uart echo <cmd>), echoes in its RX interrupt and leaves the echo after 1 s idle and the results come on the console."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
r.boot()
r.k("killall leakcam_agent", 3)          # its READY frames on UART1 would land in the bench's stream (2026-10-25)
r.sample("idle")
lat, rate, bad, runs = [], [], 0, 0
for i in range(8):
    bl("pt_uart echo uart_bench 1024", 0.3)
    out = r.k(None, 40, r"bad bytes[^\n]*\n")
    m = re.search(r"median (\d+) us, max (\d+) us, (\d+) of 200 ok", out)
    t = re.search(r"(\d+) of (\d+) bytes back in \d+ us = (\d+) B/s \(line max (\d+) B/s\), (\d+) bad", out)
    if m and t:
        runs += 1
        lat.append((int(m.group(1)), int(m.group(2))))
        rate.append((int(t.group(3)), int(t.group(4))))
        bad += int(t.group(5)) + (200 - int(m.group(3))) + (int(t.group(2)) - int(t.group(1)))
    else:
        r.obstacle(f"run {i + 1}: no uart_bench result on the console")
    r.sample(f"run {i + 1}")
    bl("pt_uart on", 1)
if rate:
    best = max(x[0] for x in rate)
    r.set(r"UART link throughput", f"{best} B/s = {best * 100 // rate[0][1]} % of the 2 Mbaud line; 16 B round trip median "
          f"{min(x[0] for x in lat)}-{max(x[0] for x in lat)} us, max {max(x[1] for x in lat)} us; {bad} bad bytes / "
          f"frames over {runs} x 1 MB ({r.date})")
r.set("share", f"n/a: the link is up from the UART driver's init (0.27 s), no step of its own ({r.date})")
r.set("energy", f"n/a: the link's own current is below the meter's step next to the K230 (test010) ({r.date})")
r.set("perday", f"n/a: a few link frames per session, negligible ({r.date})")
r.finish(f"{runs} of 8 runs of uart_bench 1024 at 2 Mbaud: {bad} errors" +
         (f", {min(x[0] for x in rate)}-{max(x[0] for x in rate)} B/s." if rate else "."), runs == 8 and bad == 0)
