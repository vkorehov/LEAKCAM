#!/usr/bin/env python3
"""test141: the full firmware on battery sleeps in HBN between its events (DESIGN.md 5.3.12, 5.3.15): the console
gone while it sleeps, check-ins on the server's interval through the sleep (the RTC wakes it), HBN wakes counted
(`hbnw`), not boots, the room reading taken at a check-in wake after the cool window, the wall clock kept through the
sleep, the USB plug wakes it. Hands-free: USB-C 1 out and back by the relay box, the server's check-in interval 20 s.
    tests/test141-battery-hbn-sleep/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, bl_back, log   # noqa: E402
import phone   # noqa: E402
from phone import SERVER, board_id, clear_leak, firmware, hand, intervals   # noqa: E402

CHECKIN = 20                     # s: sleeps of ~15 s between the check-ins
QUIET = 30                       # s: the bench's cool window (kept in HBN RAM through the sleeps)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def plain(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def device():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read())


def wait(pred, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return time.time() - t0
        time.sleep(step)
    return None


def checkins(n, secs):
    """the times and metrics of the next n check-ins at the server (last_seen_s falling back)"""
    got, prev, t0 = [], None, time.time()
    while time.time() - t0 < secs and len(got) < n:
        d = device()
        s = d.get("last_seen_s", -1)
        if prev is not None and s < prev:
            got.append((time.time(), d.get("metrics") or {}))
        prev = s
        time.sleep(0.5)
    return got


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish("not run: the board does not run the full firmware", False, switch_off=False)
    sys.exit(1)
clear_leak(dev)
intervals(dev, checkin=CHECKIN)
t_in = time.time()
bl("presence now", 1)
wait(lambda: "check-in every 20 s" in plain(bl("presence", 2)), 15)
# the boot count from a check-in of this boot: the server's last one can be older than the runner's reset before the
# test (139 -> 142 by then, 2026-11-10)
wait(lambda: device().get("last_seen_s", 1e9) <= time.time() - t_in, 15)
boots0 = (device().get("metrics") or {}).get("boots")
bl(f"climate quiet {QUIET}", 1)

# 1. battery, sleep on: the console goes (HBN) once nothing runs
phone.KEEP_AWAKE = False
try:
    if hand("USB-C 1 out (the board on its battery, HBN on)", "battery") is None:
        r.finish("not run: the relay box did not unplug USB-C 1", False, switch_off=False)
        sys.exit(1)
    t_out = time.time()
    gone = wait(lambda: not os.path.exists(BL_PORT), 60)
    check("battery: asleep (the console gone) once nothing runs", gone is not None,
          f"{gone:.0f} s after the unplug" if gone is not None else "still there after 60 s")

    # 2. check-ins through the sleep, on the interval; wakes counted, not boots; the room reading at a check-in wake
    got = checkins(4, 4 * CHECKIN + 30)
    gaps = [round(b[0] - a[0]) for a, b in zip(got, got[1:])]
    check(f"asleep: check-ins every ~{CHECKIN} s (the RTC wakes it)", len(got) >= 3 and all(
          CHECKIN - 4 <= g <= CHECKIN + 8 for g in gaps), f"gaps {gaps} s")
    m = got[-1][1] if got else {}
    check("asleep: HBN wakes counted (hbnw), boots unchanged", m.get("hbnw", 0) >= 1 and
          (boots0 is None or m.get("boots") == boots0), f"hbnw {m.get('hbnw')}, boots {boots0} -> {m.get('boots')}")
    room = [x for _, x in got if x.get("troom") is not None and 0 <= x.get("roomago", -1) <= 5]
    check(f"asleep: a check-in wake after {QUIET} s cool reads the room (troom fresh)", bool(room),
          f"troom {room[-1].get('troom')}, rhroom {room[-1].get('rhroom')}, quiet {room[-1].get('quiet')}" if room
          else f"last: troom {m.get('troom')}, roomago {m.get('roomago')}, quiet {m.get('quiet')}")
    if len(got) >= 2:
        r.set("HBN cycle", f"check-ins {gaps} s apart asleep between them, hbnw {m.get('hbnw')} since the last "
              f"good check-in, sleeping {round(time.time() - t_out)} s on battery")

    # 3. the USB plug wakes it: the console back, a wake from HBN, the clock kept
    t_plug = hand("USB-C 1 back (the bench)", "usb")
    # bl_back(): a shell that answers, with the harness's recovery of the VM's USB controller when it missed the
    # re-enumeration (2026-11-10: the board woke, the VM saw it only after the xhci reset)
    back = bl_back(45) if t_plug else None
    check("USB plug: the console back (ACOMP0 wakes it)", back is not None,
          f"{back:.1f} s after the plug" if back is not None else "not in 30 s")
    # the first line after the plug can be lost (the console came back mid-line: "k230:: command not found",
    # 2026-11-10): asked until the answer shows
    out = ""
    for _ in range(3):
        out = plain(bl("hbn", 2, r"hbn: (on|off)[^\n]*\n"))
        if "hbn: " in out:
            break
    out += plain(bl("clock", 2, r"clock: \d+"))
    check("USB plug: this boot a wake from HBN", "a wake from HBN" in out, out.strip()[:140])
    c = re.search(r"clock: (\d+)", out)
    off = int(c.group(1)) - int(time.time()) if c else None
    check("the wall clock kept through the sleeps (within 5 s)", off is not None and abs(off) <= 5,
          f"{off} s off" if off is not None else "no clock line")
finally:
    phone.KEEP_AWAKE = True
    intervals(dev)
    bl("climate quiet 10800", 1)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
