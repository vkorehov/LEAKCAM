#!/usr/bin/env python3
"""test140: temperature and humidity (DESIGN.md 5.3.15, user 2026-11-10): the room's values only after the board was
cool for the window (no K230, no USB power), the 85 %RH alarm at any reading (a check-in at once, the server's leak
"humidity high", a K230 leak check `humid`). Hands-free: USB-C 1 out and back by the relay box (the BL616 console stays
on its own port), the window shortened (`climate quiet 10`), the humidity simulated for the alarm (`climate sim`).
    tests/test140-humidity-and-room-climate/run.py
"""
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, ack_alerts, board_id, clear_leak, firmware, hand, intervals, to_firmware   # noqa: E402

QUIET = 10                       # the bench's cool window (s): the product's is 3 h
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


def state():
    """`climate`: (cool s or None when warm, rooms, alarms, reads)"""
    m = re.search(r"climate: last \S+ C \S+ %RH, (cool|warm) (\d+) s \(room after \d+ s\), reads (\d+), fails \d+, "
                  r"rooms (\d+), alarms (\d+)", plain(bl("climate", 1.5)))
    if not m:
        return None, None, None, None
    return (int(m.group(2)) if m.group(1) == "cool" else None), int(m.group(4)), int(m.group(5)), int(m.group(3))


def reading():
    """`climate now`: the reading's line (and an alarm's lines after it)"""
    return plain(bl("climate now", 3, r"climate: now: [^\n]*\n"))


def checkin_metrics(after):
    """the server's metrics of the first check-in after `after` (the board asked to check in now)"""
    bl("presence now", 1)
    got = {}

    def fresh():
        d = device()
        if d.get("last_seen_s", 1e9) <= time.time() - after:
            got.update(d.get("metrics", {}))
            return True
        return False
    wait(fresh, 15)
    return got


def k230_says(what):
    """the K230's console read on (1 s) until it has `what`"""
    r.console += cmd(r.fd, None, 1)
    return what in r.console


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


def bl_free_best():
    """the free heap at its high level: it swings ~30 KB between two levels with the K230's and the Wi-Fi's state
    (171 -> 142 KB with no leak, 2026-11-11; test109 the same), so one sample at each end is not comparable. Samples
    across a check-in, the best one; a leak lowers every level"""
    bl("presence now", 1)
    got = [f for f in (bl_free() for _ in range(6)) if f]
    return max(got) if got else None


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
clear_leak(dev)
intervals(dev)
bl("climate sim off", 1)
mem0 = bl_free_best()

# 1. on USB power: warm (the K230 on, the charger): a reading, never the room's
out = reading()
cool, rooms0, alarms0, _ = state()
check("USB: a reading, the board warm, not the room", "climate: now:" in out and ": the room" not in out and cool is None,
      out.strip()[:90])
m = checkin_metrics(time.time())
usb_t, usb_die = m.get("t"), m.get("tdie")
check("USB: the check-in carries t/rh and quiet ~0", m.get("rh", -1) >= 0 and 0 <= m.get("quiet", -1) <= 2,
      f"t {m.get('t')}, rh {m.get('rh')}, quiet {m.get('quiet')}")

# 2. on battery: the K230 off, the board cool from then; a reading after the window is the room's
if hand("USB-C 1 out (the board on its battery)", "battery") is None:
    r.finish("not run: the relay box did not unplug USB-C 1", False, switch_off=False)
    sys.exit(1)
t_cool = wait(lambda: state()[0] is not None, 30)
check("battery: cool once the K230 is off", t_cool is not None, f"{t_cool:.0f} s after the unplug" if t_cool is not None
      else "still warm after 30 s")
bl(f"climate quiet {QUIET}", 1)
wait(lambda: (state()[0] or 0) >= QUIET + 1, QUIET + 15)
t0 = time.time()
out = reading()
_, rooms1, _, _ = state()
check(f"battery, cool >= {QUIET} s: the reading is the room's", ": the room" in out and rooms1 == (rooms0 or 0) + 1,
      out.strip()[:90])
m = checkin_metrics(t0)
rt = re.search(r"climate: now: (-?\d+)\.(\d) C (\d+)\.(\d) %RH", out)
want = (int(rt.group(1)) * 10 + (1 if rt.group(1)[0] != "-" else -1) * int(rt.group(2)),
        int(rt.group(3)) * 10 + int(rt.group(4))) if rt else None
check("battery: the check-in carries troom/rhroom (that reading), roomago, quiet >= window",
      # the check-in takes its own room reading first thing (the board still cool): a second later than the
      # console's, it may differ by the sensor's step (28.9 -> 28.8 C, 2026-11-10): within 0.3 C / 1 %RH
      want is not None and m.get("troom") is not None and abs(m["troom"] - want[0]) <= 3
      and abs(m.get("rhroom", -99) - want[1]) <= 10 and 0 <= m.get("roomago", -1) <= 20
      and m.get("quiet", 0) >= QUIET, f"troom {m.get('troom')}, rhroom {m.get('rhroom')}, roomago {m.get('roomago')}, "
      f"quiet {m.get('quiet')} (reading {want})")
r.set("temp", f"on USB: board {usb_t / 10 if usb_t is not None else '?'} C (AHT20), "
      f"BL616 die {usb_die / 10 if usb_die is not None else '?'} C; on battery after {QUIET} s cool: board "
      f"{m.get('troom', 0) / 10} C; tdie {m.get('tdie', 0) / 10} C")
if want:
    r.set("Room reading", f"{want[0] / 10:.1f} C, {want[1] / 10:.1f} %RH after {QUIET} s cool on battery (bench window)")

# 3. a K230 session warms the board: the window starts again at its end, no room reading before it is over
_, rooms1, _, _ = state()                    # after step 2's check-in, which takes a room reading of its own
out = plain(bl("leak check", 60, r"k230: off \([^)]*\)[^\n]*\n"))
check("battery: the leak check's K230 session ran and ended", "k230: session rtc" in out and "leak: DONE" in out
      and "k230: off" in out, re.search(r"leak: DONE[^\r\n]*", out).group(0) if "leak: DONE" in out else out.strip()[-120:])
out = reading()
cool, rooms2, _, _ = state()
check("right after the K230 session: cool < window, no room reading", ": the room" not in out and rooms2 == rooms1
      and cool is not None and cool < QUIET, out.strip()[:90])

# 4. the humidity alarm on battery: 85 %RH or more at any reading -> check-in at once, the server's leak, K230 `humid`
bl("climate sim 84", 1)                      # whole percent: 84.0 %RH
out = reading()
check("84 %RH: no alarm", "alarm" not in out and "leak" not in device(), out.strip()[:90])
t0 = time.time()
bl("climate sim 90", 1)
out = plain(bl("climate now", 60, r"k230: off \([^)]*\)[^\n]*\n"))
check("90 %RH: the alarm and a K230 leak check `humid`", "humidity 90.0 %RH >= 85: alarm" in out
      and "leak: check humid: K230 session" in out, "seen" if "alarm" in out else out.strip()[:120])
got = wait(lambda: (device().get("leak") or {}).get("why") == "humidity high", 20)
check("90 %RH: the server's leak `humidity high` at once", got is not None,
      f"{time.time() - t0:.1f} s after the reading" if got is not None else f"leak {device().get('leak')}")
if got is not None:
    r.set("Humidity alarm", f"reading -> the server's leak in {time.time() - t0:.1f} s (on battery, K230 check `humid`)")
ack_alerts()
bl("climate sim off", 1)
clear_leak(dev)

# 5. USB back: warm again; the alarm on USB: CHECK humid to the running K230
if hand("USB-C 1 back (the bench)", "usb") is None:
    r.finish("stopped: USB-C 1 not back", False, switch_off=False)
    sys.exit(1)
check("USB back: warm again", wait(lambda: state()[0] is None, 30) is not None)
r.fd = openp(K230_PORT)                      # the K230 boots on USB power: its job's own check at the start first (a
cmd(r.fd, None, 1)                           # CHECK during the boot is answered by that check's DONE, 2026-11-10);
r.console = ""                               # the battery session's lines still buffered dropped (a stale "job: done")
up = wait(lambda: k230_says("job: done"), 90, 0)
wait(lambda: "one running" not in plain(bl("leak", 1)), 30)
check("USB back: the K230 up, its start check done", up is not None, f"{up:.0f} s" if up is not None else "not in 90 s")
r.console = ""
bl("climate sim 90", 1)
out = plain(bl("climate now", 5, r"leak: DONE[^\n]*\n"))


wait(lambda: k230_says("job: CHECK humid"), 60, 0)
check("USB, 90 %RH: CHECK humid to the running K230", "leak: check humid: CHECK to the running K230" in out
      and "job: CHECK humid" in r.console, "seen" if "CHECK humid" in r.console else
      (out.strip()[-160:] + " | " + plain(bl("leak", 1)).strip()[-120:]))
ack_alerts()
bl("climate sim off", 1)
bl("climate quiet 10800", 1)                 # the product's window back (a reset restores it too)
clear_leak(dev)
mem1 = bl_free_best()

# 6. the setup firmware's alarm: a network stored -> back to the full firmware (which tells the server)
went = to_firmware("setup", "switch_to_setup lost", 30)
check("switch_to_setup lost: in the setup firmware", went is not None)
out = plain(bl("climate sim 90", 1) + bl("climate now", 5, r"humidity alarm[^\n]*\n"))
back = to_firmware("full", None, 60)
check("setup, 90 %RH: the alarm switches back to the full firmware", "humidity alarm: to the full firmware" in out
      and back is not None, f"full after {back:.0f} s" if back is not None else out.strip()[-120:])
ack_alerts()
clear_leak(dev)
check("the BL616's free heap kept", mem0 and mem1 and mem1[0] >= mem0[0] - 1024, f"{mem0} -> {mem1}")
r.set("Memory BL616", f"BL616 free {mem0} -> {mem1} (free, min)")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
