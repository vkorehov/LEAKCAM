#!/usr/bin/env python3
"""test118 automated: the app's status icons follow the board (user 2026-11-01). The app's row for a LEAKCAM shows the
Bluetooth icon while the board is in BLE range, the Wi-Fi icon while it is present at the server, and its text in red
when it has no presence. The app logs every redraw of a row ("row <id> wifi 0|1 red 0|1 bat <pct> chg 0|1"); the
runner reads those. Since the app's 11-07 screens (commit 4c904fb: the LEAKCAMs list is the server's view, no BLE wake)
that list shows no Bluetooth icon; the Bluetooth icon and bars are on the Setup list's rows (test129). So the checks
here: Wi-Fi and red follow presence, and BLE in or out of range does not change the LEAKCAMs row (2026-11-12: the old
"ble" field in the log made every check read "row None"). On the test firmware (no presence of its own): the bench checks in at the server under the board's own ID (as
the board would), and the board's advertising goes off and on (`pt_ble adv off|on`). USB-C 1 in, the phone on adb,
unlocked; nothing by hand.
    tests/test118-app-presence-icons/run.py
"""
import fcntl
import os
import re
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import adb, as_board, board_id, intervals, only_boards, ready   # noqa: E402

CHECKIN_S = 10                      # for the run; the bench server's default is 60 (offline after three missed)
PKG = "com.leakcam.setup"

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test118: another run.py is running")

r = Run(__file__)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def checkin_interval(s):
    """the board's check-in interval, per device at the server (DESIGN.md 5.3.12): taken at its next check-in"""
    intervals(board_id(), checkin=s)


present = threading.Event()      # set: the bench checks in for the board every CHECKIN_S


def checker(dev):
    while True:
        if present.is_set():
            try:
                as_board(dev, f"/v1/poll?id={dev}", b"ssid=t118")   # as the board: mTLS with its certificate
            except OSError as e:
                log("check-in failed", repr(e))
        time.sleep(CHECKIN_S)


def row(dev):
    """the app's last redraw of the board's row: (wifi, red) or None"""
    m = re.findall(rf"row {dev} wifi (\d) red (\d)", adb("logcat", "-d", "-s", "LeakcamSetup:I"))
    return tuple(int(x) for x in m[-1]) if m else None


def wait_row(dev, name, want, secs):
    """want: (wifi, red) with None for "any"; the time until the row shows it"""
    t0, got = time.time(), None
    while time.time() - t0 < secs:
        got = row(dev)
        if got and all(w is None or w == g for w, g in zip(want, got)):
            check(name, True, f"{time.time() - t0:.0f} s, row {got}")
            return True
        time.sleep(1)
    check(name, False, f"not within {secs} s, row {got}")
    return False


def add_only(dev):
    """the app's list (the server's for this phone, DESIGN.md 5.3.5) holds just this board, as a setup leaves it"""
    adb("shell", "am", "force-stop", PKG)
    only_boards(dev)


if not os.path.exists(BL_PORT) or "pt_ble" not in bl("help", 2):
    r.obstacle("the BL616 test firmware with pt_ble is needed (tests/firmware/bl616_powertest)")
    r.finish("not run: no pt_ble.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
bl("pt_ble on", 5, r"ble: ready as|already on")
bl("pt_ble adv on", 2, r"advertising on")
checkin_interval(CHECKIN_S)
threading.Thread(target=checker, args=(dev,), daemon=True).start()
try:
    present.set()
    time.sleep(2)
    add_only(dev)
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")   # the status screen: presence every 10 s, scan 30 s
    # present (BLE advertising too): the Wi-Fi icon, not red
    wait_row(dev, "start: the Wi-Fi icon, not red", (1, 0), 60)
    # out of BLE range (advertising off): the LEAKCAMs row follows the server, not BLE - unchanged after a scan or two
    bl("pt_ble adv off", 2, r"advertising off")
    time.sleep(70)
    check("BLE gone: the row unchanged (Wi-Fi icon, not red)", row(dev) == (1, 0), str(row(dev)))
    # presence lost (no check-in for three intervals): the Wi-Fi icon goes, the row red
    present.clear()
    wait_row(dev, "presence gone: the Wi-Fi icon goes, the row red", (0, 1), 3 * CHECKIN_S + 40)
    # presence back: the Wi-Fi icon again, not red
    present.set()
    wait_row(dev, "presence back: the Wi-Fi icon again, not red", (1, 0), CHECKIN_S + 30)
    # BLE back while present: the row unchanged
    bl("pt_ble adv on", 2, r"advertising on")
    time.sleep(40)
    check("BLE back: the row unchanged (Wi-Fi icon, not red)", row(dev) == (1, 0), str(row(dev)))
    # presence lost while in BLE range: red all the same (BLE is no presence)
    present.clear()
    wait_row(dev, "presence gone in BLE range: no Wi-Fi icon, the row red", (0, 1), 3 * CHECKIN_S + 40)
    r.sample("after the checks")
finally:
    present.clear()
    bl("pt_ble adv on", 2)
    checkin_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
