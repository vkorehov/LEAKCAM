#!/usr/bin/env python3
"""test138: time without NTP (DESIGN.md 5.3.12), on USB power at the bench, no hand: the server's X-Time on every
answer -> the BL616's clock -> ;t= on every BL616 frame -> the K230's clock; either clock changed only when it is more
than 5 s off. The host's UTC is the reference (the bench PC runs NTP).
    tests/test138-clock-without-ntp/run.py
"""
import calendar
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def bl_clock():
    """(the BL616's clock, how often the server's answers set it) from `clock`"""
    m = re.search(r"clock: (\d+) \(set (\d+) times\)", bl("clock", 2, r"set \d+ times"))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def bl_set(unix_s):
    """the BL616's clock moved by hand (`clock <unix s>`, past the 5 s rule)"""
    bl(f"clock {unix_s}", 2)


def next_checkin(secs=80):
    """the board's next check-in, asked for (`presence now`: no wait for its interval); seconds until the server saw
    it (its last_seen_s falls back)"""
    t0 = time.time()
    bl("presence now", 1)
    while time.time() - t0 < secs:
        with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
            seen = json.loads(f.read()).get("last_seen_s", -1)
        if 0 <= seen and seen + 1 <= time.time() - t0:   # its age (whole s, floored) younger than the ask: after it
            return time.time() - t0
        time.sleep(0.5)
    return None


def k_clock():
    """the K230's clock (its shell's `date`), unix s; None when it does not answer. Only right after the program set
    it: the shell's `date` reads it, but setting it there does not reach the program (2026-11-07)"""
    m = re.search(r"\w{3} (\w{3}) +(\d+) (\d+):(\d+):(\d+) (\d{4})", r.k("date", 2))
    if not m:
        return None
    return calendar.timegm(time.strptime(" ".join(m.groups()), "%b %d %H %M %S %Y"))


def bl616_frame():
    """a BL616 frame to the K230 now (CHECK: `leak check`), its ;t= with it; the K230's lines meanwhile kept"""
    bl("leak check", 5, r"leak: DONE [^\n]*\n")
    r.console += cmd(r.fd, None, 3)              # the K230's lines reach the bench after the BL616's DONE: >1 s seen


r = Run(__file__)
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
dev = board_id()
t = time.time()
while time.time() - t < 70 and not ((k := k_clock()) and abs(k - time.time()) <= 5):   # the K230's session up
    time.sleep(0.5)

# 1. the server: X-Time on every answer
for path in ("/", "/v1/devices/bad"):
    try:
        resp = urllib.request.urlopen(SERVER + path, timeout=5)
    except urllib.error.HTTPError as e:
        resp = e
    xt = resp.headers.get("X-Time")
    check(f"the server's answer to {path} ({resp.status}) carries X-Time within 2 s of UTC",
          xt is not None and abs(int(xt) - time.time()) <= 2, f"X-Time {xt}, off {int(xt) - time.time():+.1f} s" if xt else "none")

# 2. the BL616's clock from the server
c, sets = bl_clock()
# set from the server at least once since the battery, kept on the RTC through resets (HBN RAM, 2026-11-10): after a
# reset a clock already within 5 s is not set again (set 0 times this boot is right); step 7 proves the server's set
check("the BL616's clock: within 5 s of UTC (the server's, kept on the RTC)", c is not None and abs(c - time.time()) <= 5,
      f"off {c - time.time():+.1f} s, set {sets} times" if c else "no clock line")

# 3. not stepped for a small drift: two more check-ins leave the count (asked at once: `presence now`, no 2 x 60 s wait)
t0 = time.time()
for _ in range(2):
    next_checkin(15)
c2, sets2 = bl_clock()
check("the BL616's clock: two check-ins later still set the same number of times (drift <= 5 s: no step)",
      sets2 == sets and c2 is not None and abs(c2 - time.time()) <= 5, f"set {sets} -> {sets2}, off {c2 - time.time():+.1f} s")

# 4. the K230's clock from the BL616
k = k_clock()
check("the K230's clock: within 5 s of UTC (from ;t=)", k is not None and abs(k - time.time()) <= 5,
      f"off {k - time.time():+.0f} s" if k else "no date")

# 5. the BL616 3 s ahead: its next frame leaves the K230's clock (<= 5 s)
bl_set(int(time.time()) + 3)
r.console = ""
bl616_frame()
check("the BL616 3 s ahead: the K230 does not step to its frame (<= 5 s)", "clock:" not in r.console,
      "a clock line" if "clock:" in r.console else "no clock line")

# 6. the BL616 30 s ahead: the K230 follows its next frame
bl_set(int(time.time()) + 30)
r.console = ""
bl616_frame()
m = re.search(r"clock: (\d+) from the BL616 \(was ([+-]\d+) s off\)", r.console)
check("the BL616 30 s ahead: the K230 steps to its next frame (logged about -30 s off)",
      m is not None and -33 <= int(m.group(2)) <= -27, m.group(0) if m else "no clock line")

# 7. the server sets the BL616 back at its next answer (> 5 s off), and the K230 follows again
bl_set(int(time.time()) + 30)               # again: a check-in during step 6 may have set it back already
c0, s0 = bl_clock()
got = next_checkin()
c, s1 = bl_clock()
check("the BL616 30 s off: the next check-in's X-Time sets it back (within 2 s of UTC)",
      got is not None and s1 == s0 + 1 and c is not None and abs(c - time.time()) <= 2,
      f"check-in {got:.0f} s later, set {s0} -> {s1}, off {c - time.time():+.1f} s" if got and c else f"no check-in ({got})")
r.console = ""
bl616_frame()
m = re.search(r"clock: (\d+) from the BL616 \(was ([+-]\d+) s off\)", r.console)
check("the K230 follows back (about +30 s off before)", m is not None and 27 <= int(m.group(2)) <= 33,
      m.group(0) if m else "no clock line")

# 8. the BL616 3 s ahead: the server's next answer leaves it (<= 5 s)
bl_set(int(time.time()) + 3)
c0, s0 = bl_clock()
got = next_checkin()
c, s1 = bl_clock()
check("the BL616 3 s off: the next check-in leaves it (<= 5 s: no step)", got is not None and s1 == s0
      and c is not None and 1 <= c - time.time() <= 5, f"set {s0} -> {s1}, off {c - time.time():+.1f} s" if c else "no clock")
bl_set(int(time.time()))                     # back on time for what follows

# 9. the first frame of a session already carries the time (the WAKE): `k230 off`, the full firmware's restart
bl("k230 off", 3)
r.console = ""
bl("leak check", 1)                          # a session now (its reason rtc), not the USB restart 30 s later
t1 = time.time()
while time.time() - t1 < 70 and "WAKE" not in r.console:
    r.console += cmd(r.fd, None, 1)
lines = [ln for ln in r.console.splitlines() if "clock:" in ln or "WAKE" in ln]
first = next((i for i, ln in enumerate(lines) if "clock:" in ln), None)
wake = next((i for i, ln in enumerate(lines) if "WAKE" in ln), None)
check("a new session: the K230's clock set before its WAKE step (the time on the first BL616 frame)",
      first is not None and wake is not None and first < wake, " | ".join(lines[:3]) or "nothing")

r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
