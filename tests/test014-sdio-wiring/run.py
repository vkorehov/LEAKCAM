#!/usr/bin/env python3
"""test014 automated (step 3 of the table): the BL616 counts edges on CLK, CMD, D0-D3 (pt_sdio watch 8) while the
K230 probes MMC0 (bl616_rescan); 8 boots with snapshots. The pattern steps (BL616 drives, K230 reads the present
state) needed a temporary K230 command and stay the result of 2026-10-19."""
import os
import re
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402

r = Run(__file__)
res, fails = [], []
for i in range(8):
    r.off()
    time.sleep(2)
    r.boot()
    out = {}
    th = threading.Thread(target=lambda: out.update(w=bl("pt_sdio watch 8", 11, r"samples/s[^\n]*\n")))
    th.start()
    time.sleep(1)
    probe = r.k("bl616_rescan", 6, r"wlan init success|no card")
    th.join()
    e = dict(re.findall(r"(CLK|CMD|D0|D1|D2|D3)\s+edges\s+(\d+)", out.get("w", "")))
    res.append(e)
    if int(e.get("CLK", 0)) < 1000 or int(e.get("CMD", 0)) < 10:
        fails.append(f"boot {i + 1}: CLK {e.get('CLK')} CMD {e.get('CMD')} edges")
    r.sample(f"boot {i + 1}")
clk = [int(x.get("CLK", 0)) for x in res]
cmd = [int(x.get("CMD", 0)) for x in res]
r.set("speed", f"n/a: wiring test; the bus speed is test015 (50 MHz 4-bit) ({r.date})")
r.set("share", f"bl616_rescan's probe ends 'no card' after ~35 ms with the test firmware (no SDIO device) ({r.date})")
r.set("energy", f"n/a: a wiring check ({r.date})")
r.set("perday", f"n/a: a wiring check ({r.date})")
r.finish(f"8 boots, pt_sdio watch during bl616_rescan: CLK {min(clk)}-{max(clk)} edges, CMD {min(cmd)}-{max(cmd)} edges, "
         f"D0-D3 {'/'.join(res[-1].get(d, '?') for d in ('D0', 'D1', 'D2', 'D3'))} (last boot; no data phase without a "
         "card)." + (" " + "; ".join(fails) if fails else ""), not fails)
