#!/usr/bin/env python3
"""test134: a Wi-Fi change through the server (DESIGN.md 5.3.13), no hand, no phone: PUT /v1/devices/<id>/wifi, the
board takes it from its next check-in's answer (asked at once: `presence now`), tests it with the whole check, stores it
only when it passes; the result in the next check-in (`wchg`) and at the server (`wifi_change`). A wrong passphrase:
the old network kept, the failure reported, the 5-min wake mode (check-in 300 s); then the right one: stored, the mode
left. The credentials from ~/.leakcam_wifi, never printed.
    tests/test134-wifi-change-via-server/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import log, wifi_credentials   # noqa: E402
from phone import SERVER, board_checkin, board_id, firmware, intervals   # noqa: E402

USER_S = 15
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def device():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read())


def put_wifi(ssid, psk):
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/wifi", method="PUT",
                                 data=json.dumps({"ssid": ssid, "psk": psk}).encode())
    with urllib.request.urlopen(req, timeout=10) as f:
        return json.loads(f.read())


def change(ssid, psk, want):
    """PUT, a check-in now, the board's result line and the server's state; (state byte, seconds to the result)"""
    put_wifi(ssid, psk)
    t0 = time.time()
    out = re.sub(r"\x1b\[[0-9;]*m", "", bl("presence now", 60, r"the new Wi-Fi: state 0x[0-9a-f]{2}[^\n]*\n"))
    m = re.search(r"the new Wi-Fi: state 0x([0-9a-f]{2})", out)
    took = time.time() - t0
    t1 = time.time()
    while time.time() - t1 < 20 and device().get("wifi_change") != want:
        time.sleep(0.5)
    return (int(m.group(1), 16) if m else None), took, device().get("wifi_change")


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish(f"not run: the board runs {firmware() or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
ssid, psk = wifi_credentials()
intervals(dev, checkin=5)
try:
    # 1. a wrong passphrase: the old network kept, the failure at the server, the 5-min wake mode
    st, took, at_server = change(ssid, "wrong-passphrase-1", "failed")   # nothing of the real one in it
    check("wrong passphrase: the board tried it and kept the old network (state 0x11 wrong password)", st == 0x11,
          f"state {st:#04x}" if st is not None else "no result line")
    check("wrong passphrase: the server has the failure (wifi_change failed)", at_server == "failed", str(at_server))
    t = time.time()
    while time.time() - t < 20 and board_checkin() != 300:
        bl("presence now", 1)
        time.sleep(3)
    check("wrong passphrase: the board in the 5-min wake mode (check-in every 300 s)", board_checkin() == 300,
          f"check-in every {board_checkin()} s")
    check("wrong passphrase: still online on the old network", device().get("online") is True, str(device().get("ssid")))
    # 2. the right one: stored, reported, the mode left
    st, took2, at_server = change(ssid, psk, "saved")
    check("right passphrase: tested and stored (state 0x04 saved)", st == 0x04, f"state {st:#04x}" if st is not None else "none")
    check(f"right passphrase: the result within {USER_S} s of the check-in", took2 < USER_S, f"{took2:.1f} s")
    check("right passphrase: the server has it saved, the mode left", at_server == "saved", str(at_server))
    bl("presence now", 1)
    time.sleep(4)
    check("back on the run's 5 s check-in", board_checkin() == 5, f"check-in every {board_checkin()} s")
    r.set("Speed / rate against the top", f"PUT -> the board's result: wrong passphrase {took:.1f} s, right "
          f"{took2:.1f} s (the check-in asked at once)")
finally:
    intervals(dev)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
