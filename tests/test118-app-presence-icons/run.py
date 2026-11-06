#!/usr/bin/env python3
"""test118 automated: the app's status icons follow the board (user 2026-11-01). The app's row for a LEAKCAM shows the
Bluetooth icon while the board is in BLE range, the Wi-Fi icon while it is present at the server, and its text in red
when it has no presence. The app logs every redraw of a row ("row <id> wifi 0|1 ble 0|1 red 0|1"); the runner reads
those. On the test firmware (no presence of its own): the bench checks in at the server under the board's own ID (as
the board would), and the board's advertising goes off and on (`pt_ble adv off|on`). USB-C 1 in, the phone on adb,
unlocked; nothing by hand.
    tests/test118-app-presence-icons/run.py
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log   # noqa: E402
from phone import adb, board_id, only_boards, ready   # noqa: E402

SERVER = "http://192.168.1.159:8090"
PI = "pi@192.168.1.159"
POLL_S = 10                      # for the run; the server's default is 60 (offline after three missed)
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


def poll_interval(s):
    # only the poll line: the env file also holds the pairing PIN key (server/deploy.sh)
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, f"sed -i 's/^LEAKCAM_POLL_S=.*/LEAKCAM_POLL_S={s}/' "
                    "~/leakcam-server/env && sudo systemctl restart leakcam-server"], check=True, timeout=30)
    time.sleep(1)


present = threading.Event()      # set: the bench checks in for the board every POLL_S


def checker(dev):
    while True:
        if present.is_set():
            try:
                urllib.request.urlopen(f"{SERVER}/v1/poll?id={dev}&ssid=t118", timeout=5).read()
            except OSError as e:
                log("check-in failed", repr(e))
        time.sleep(POLL_S)


def row(dev):
    """the app's last redraw of the board's row: (wifi, ble, red) or None"""
    m = re.findall(rf"row {dev} wifi (\d) ble (\d) red (\d)", adb("logcat", "-d", "-s", "LeakcamSetup:I"))
    return tuple(int(x) for x in m[-1]) if m else None


def wait_row(dev, name, want, secs):
    """want: (wifi, ble, red) with None for "any"; the time until the row shows it"""
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
poll_interval(POLL_S)
threading.Thread(target=checker, args=(dev,), daemon=True).start()
try:
    present.set()
    time.sleep(2)
    add_only(dev)
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")   # the status screen: presence every 10 s, scan 30 s
    # in range and present: both icons, not red
    wait_row(dev, "start: Wi-Fi and Bluetooth icons, not red", (1, 1, 0), 60)
    # out of BLE range (advertising off): the Bluetooth icon goes (a board unseen 45 s, scans every 30 s)
    bl("pt_ble adv off", 2, r"advertising off")
    wait_row(dev, "BLE gone: the Bluetooth icon goes", (None, 0, None), 120)
    check("BLE gone: the Wi-Fi icon stays, not red", row(dev) == (1, 0, 0), str(row(dev)))
    # presence lost (no check-in for three intervals): the Wi-Fi icon goes, the row red
    present.clear()
    wait_row(dev, "presence gone: the Wi-Fi icon goes, the row red", (0, 0, 1), 3 * POLL_S + 40)
    # presence back: the Wi-Fi icon again, not red
    present.set()
    wait_row(dev, "presence back: the Wi-Fi icon again, not red", (1, 0, 0), POLL_S + 30)
    # BLE back: the Bluetooth icon again (the next scan, every 30 s)
    bl("pt_ble adv on", 2, r"advertising on")
    wait_row(dev, "BLE back: the Bluetooth icon again", (1, 1, 0), 60)
    # both gone at once: no icon, red
    bl("pt_ble adv off", 2, r"advertising off")
    present.clear()
    wait_row(dev, "both gone: no icon, the row red", (0, 0, 1), 3 * POLL_S + 90)
    r.sample("after the checks")
finally:
    present.clear()
    bl("pt_ble adv on", 2)
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
