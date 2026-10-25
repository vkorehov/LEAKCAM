#!/usr/bin/env python3
"""test024 automated: pt_alive 30 after the agent starts (heartbeat GPIO2 -> BL616 IO01), then rails off and no
back-feed (pt_status 0 of 5 pull-ups); 8 boots with snapshots."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402,F401

r = Run(__file__)
res, fails = [], []
for i in range(8):
    r.boot()
    out = bl("pt_alive 10", 13, r"first edge[^\n]*\n|edges 0[^\n]*\n")
    m = re.search(r"edges (\d+)", out)
    hp = re.search(r"half-period min (\d+) / avg (\d+) / max (\d+) ms", out)
    if hp:
        res.append(tuple(int(x) for x in hp.groups()))
    if not m or int(m.group(1)) < 15:
        fails.append(f"boot {i + 1}: {m.group(1) if m else 'no'} edges in 10 s")
    r.sample(f"boot {i + 1}")
    r.off()
    st = bl("pt_status", 2)
    if "0 of 5 SDIO pull-ups high" not in st:
        fails.append(f"boot {i + 1}: back-feed after pt_off")
summ = (f"half-period {min(x[0] for x in res)} / {sum(x[1] for x in res) // len(res)} / {max(x[2] for x in res)} ms "
        f"(min / avg / max over {len(res)} boots, agent 500)" if res else "no half-period reported")
r.finish(f"8 boots, pt_alive 10 after each: {summ}; after pt_off no back-feed (0 of 5 pull-ups)" +
         ("; " + "; ".join(fails) if fails else "."), not fails, switch_off=False)
