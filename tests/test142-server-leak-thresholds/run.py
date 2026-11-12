#!/usr/bin/env python3
"""test142: the leak thresholds from the server (DESIGN.md 5.3.16, user 2026-11-10): the probe's comparator reference
and the humidity alarm come as attributes of the check-in (presence) answer ("probe_mv", "humid"), set per device by
PUT /v1/devices/<id>/thresholds; the board writes them to flash only when they differ from the stored ones, keeps them
through a reset with the server down and in the setup firmware, reports the ones in use (thmv, thrh) and alarms at
them. On USB power at the bench, no hand.
    tests/test142-server-leak-thresholds/run.py
"""
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import bl_back, log   # noqa: E402
from phone import SERVER, ack_alerts, board_id, clear_leak, firmware, intervals, server_up, to_firmware   # noqa: E402

PROBE, HUMID = 900, 70           # the run's values: neither the default (1100 mV, 85 %RH)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def plain(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def wait(pred, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return time.time() - t0
        time.sleep(step)
    return None


def device():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read())


def put(probe_mv, humid):
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/thresholds", method="PUT",
                                 data=json.dumps({"probe_mv": probe_mv, "humid": humid}).encode())
    try:
        with urllib.request.urlopen(req, timeout=5) as f:
            return f.status
    except urllib.error.HTTPError as e:
        return e.code


def board():
    """`thresholds`: (probe mV, humidity %RH, from flash) or None; writes(): its flash writes this boot"""
    m = re.search(r"thresholds: probe (\d+) mV, humidity (\d+) %RH, (from the server|defaults)[^,]*, flash writes "
                  r"(\d+)", plain(bl("thresholds", 1.5)))
    if not m:
        return None
    board.writes = int(m.group(4))
    return int(m.group(1)), int(m.group(2)), m.group(3) == "from the server"


def writes():
    return board.writes if board() else None


def armed():
    """the probe comparator's reference as armed (`leak`)"""
    m = re.search(r"reference (\d+) mV", plain(bl("leak", 1.5)))
    return int(m.group(1)) if m else None


def checkin(secs=15):
    """a check-in now (`presence now`): the server's metrics of it once it is in"""
    t0 = time.time()
    bl("presence now", 1)
    m = {}

    def fresh():
        d = device()
        if d.get("last_seen_s", 1e9) <= time.time() - t0:
            m.update(d.get("metrics", {}))
            return True
        return False
    wait(fresh, secs)
    return m


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
clear_leak(dev)
intervals(dev)
bl("climate sim off", 1)

# 1. the defaults from the server: in use and reported
check("PUT 0: the server back to its defaults", put(0, 0) == 200)
m = checkin()
wait(lambda: (board() or (0,))[:2] == (1100, 85), 15)
b = board()
check("defaults: the board on 1100 mV / 85 %RH, the check-in reports them (thmv, thrh)",
      b and b[:2] == (1100, 85) and (m.get("thmv"), m.get("thrh")) == (1100, 85), f"board {b}, metrics thmv "
      f"{m.get('thmv')} thrh {m.get('thrh')}")

# 2. new values: taken at the next check-in, written to flash once
t0 = time.time()
w0 = writes()
check(f"PUT {PROBE} mV / {HUMID} %RH accepted", put(PROBE, HUMID) == 200)
checkin()
took = wait(lambda: board() == (PROBE, HUMID, True), 15)
check("the board on them at its next check-in (in flash: from the server)", took is not None,
      f"{time.time() - t0:.1f} s after the PUT" if took is not None else f"board {board()}")
w1, ref = writes(), armed()
check("one flash write, the probe's comparator re-armed at it", w0 is not None and w1 == w0 + 1 and ref == PROBE,
      f"flash writes {w0} -> {w1}, reference {ref} mV")
m = checkin()
check("the next check-in reports them (thmv, thrh)", (m.get("thmv"), m.get("thrh")) == (PROBE, HUMID),
      f"thmv {m.get('thmv')} thrh {m.get('thrh')}")
if took is not None:
    r.set("Thresholds taken", f"{PROBE} mV / {HUMID} %RH in use {time.time() - t0:.1f} s after the PUT (triggered "
          f"check-in)")

# 3. written only when different: the same values again, no flash write
checkin()
checkin()
w2 = writes()
check("the same values at two more check-ins: no flash write", w2 == w1, f"flash writes {w1} -> {w2}")

# 4. kept in flash through a reset with the server down (no answer to take them from)
pi = ["ssh", "-o", "BatchMode=yes", "pi@192.168.1.159"]
subprocess.run(pi + ["sudo systemctl stop leakcam-server"], timeout=30)
try:
    bl("reboot", 1)
    bl_back(30)
    kept = None
    t = time.time()
    while kept is None and time.time() - t < 30:
        kept = board()
finally:
    subprocess.run(pi + ["sudo systemctl start leakcam-server"], timeout=30)
    server_up()
ref = armed()
check("a reset with the server down: the stored values in use, the probe armed at them",
      kept == (PROBE, HUMID, True) and ref == PROBE, f"board {kept}, reference {ref} mV")

# 5. the humidity alarm at the server's value: 75 %RH alarms at 70 (not at the default 85), the server's leak too
bl("climate sim 75", 1)
t0 = time.time()
out = plain(bl("climate now", 5, r"climate: humidity[^\n]*\n"))
check(f"75 %RH: the alarm at {HUMID} %RH", f"humidity 75.0 %RH >= {HUMID}: alarm" in out, out.strip()[-120:])
got = wait(lambda: (device().get("leak") or {}).get("why") == "humidity high", 20)
check("75 %RH: the server's leak `humidity high` (judged at the device's threshold)", got is not None,
      f"{time.time() - t0:.1f} s" if got is not None else f"leak {device().get('leak')}")
bl("climate sim off", 1)
ack_alerts()
clear_leak(dev)
wait(lambda: "one running" not in plain(bl("leak", 1)), 60)

# 6. the setup firmware keeps them (the same flash): its humidity alarm at the server's value
went = to_firmware("setup", "switch_to_setup lost", 30)
b = board() if went is not None else None
check("the setup firmware: the stored values in use", b == (PROBE, HUMID, True), f"board {b}")
# back the way a cable start goes (test128): a reset; the setup firmware checks the stored network and switches itself
# (`switch_to_full` only reports that check's state)
bl("reboot", 1)
back = to_firmware("full", None, 60)
check("back to the full firmware", back is not None)

# 7. back to the defaults: written once, in use
put(0, 0)
checkin()
took = wait(lambda: (board() or (0,))[:2] == (1100, 85), 15)
ref = armed()
check("PUT 0: the board back on 1100 mV / 85 %RH, the probe re-armed", took is not None and ref == 1100,
      f"board {board()}, reference {ref} mV")
ack_alerts()
clear_leak(dev)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
