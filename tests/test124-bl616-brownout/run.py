#!/usr/bin/env python3
"""test124 step 2 on the product pair: the reset record (GLB reset recorder bits, `rst` in `metrics` and in every
check-in's metrics at the server) after each reset made without hands: software (`reboot`), watchdog (`crash hang`),
exception (`crash test`). A software reset and a watchdog reset leave different bits, so a brown-out's signature
(steps 3-6, a lab supply) can be told apart; an exception resets through the watchdog (the crash recorder), so it
shares the watchdog's bits and the crash record names it (exception / watchdog hang; 2026-11-08: 80, 88, 88). Step 1 (the BOD register) was read on the setup test bed on 2026-11-02 (HISTORY.md); the product has no
command for it and runs the same SDK startup (System_BOD_Init). No hands.
    tests/test124-bl616-brownout/run.py      (the product pair, the full firmware, USB-C 1 in)
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

checks, rst = [], {}


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def reset_by(c):
    """the reset; seconds until the firmware answers again, or None"""
    t0 = time.time()
    bl(c, 0.5)
    while os.path.exists(BL_PORT) and time.time() - t0 < 15:
        time.sleep(0.1)
    while time.time() - t0 < 40:
        if os.path.exists(BL_PORT) and firmware():
            return time.time() - t0
        time.sleep(0.3)
    return None


def board_rst():
    m = re.search(r"rst=([0-9a-f]{2})", bl("metrics", 3, r"rst=[0-9a-f]{2}"))
    return m.group(1) if m else None


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish(f"not run: the board runs {firmware() or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
try:
    for c, kind in [("reboot", "software"), ("crash hang", "watchdog"), ("crash test", "exception")]:
        secs = reset_by(c)
        check(f"{kind} reset (`{c}`): the full firmware back", secs is not None, f"{secs:.1f} s" if secs else "")
        rst[kind] = board_rst()
        check(f"{kind} reset: the reset record read", rst[kind] is not None, f"rst={rst[kind]}")
        log(f"reset record after a {kind} reset: rst={rst[kind]}")
    check("software and watchdog resets leave different records (a brown-out's can be told apart)",
          None not in rst.values() and rst["software"] != rst["watchdog"], str(rst))
    check("an exception resets through the watchdog (its record = the watchdog's; the crash record names it)",
          rst["exception"] == rst["watchdog"], str(rst))
    bl("presence now", 20, r"check-in state 0x[0-9a-f]+")
    time.sleep(2)
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        at_server = json.loads(f.read()).get("metrics", {}).get("rst")
    check("the server has the last reset record (check-in metrics)", at_server == rst.get("exception"),
          f"server rst={at_server}")
    r.set("speed", "reset records on the product: " + ", ".join(f"{k} rst={v}" for k, v in rst.items()) + f" ({r.date})")
finally:
    bl("crash clear", 2)                     # the deliberate crashes: not left for the next run's crash check
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
