#!/usr/bin/env python3
"""test119 automated: a phone wakes a present board's BLE through the server (user 2026-11-01: "stay notified by the
phone"). On the product pair (firmware/bl616_full in slot 0, bl616_setup in slot 1): the full firmware has no BLE. The
app shows a present board out of range with a Wake button; the tap asks the server (POST /v1/devices/<id>/wake), the
board's next check-in gets "ble":1 and the full firmware switches to the setup firmware ("wake"), which advertises for
2 minutes; the app finds it and its button turns into "Change the Wi-Fi"; the Wi-Fi is changed from the app (back to
the full firmware). A second wake within 2 minutes is refused (429); a wake nobody follows ends by itself after 2
minutes (back to the full firmware). USB-C 1 in, the phone on adb, unlocked; nothing by hand.
--battery: the wake, the change and the unanswered wake with USB-C 1 out (hands: out before the wake, in at the end):
the setup firmware sleeps with its console off there, so the app's Bluetooth icon and the phone's scan stand for "the
setup firmware" (bugs/bug-006: tests on USB never began its sleep).
    tests/test119-wake-through-server/run.py [--battery]
"""
import fcntl
import os
import re
import sys
import time
import urllib.error
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, board_checkin, board_id, firmware, hand, intervals, online, only_boards, op, product, ready, seen   # noqa: E402

SERVER = "http://192.168.1.159:8090"
CHECKIN_S = 5                       # for the run (the firmware's minimum): the wake waits for the next check-in
PKG = "com.leakcam.setup"
BATTERY = "--battery" in sys.argv

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test119: another run.py is running")

r = Run(__file__)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def checkin_interval(s):
    """the board's check-in interval, per device at the server (DESIGN.md 5.3.12): taken at its next check-in"""
    intervals(board_id(), checkin=s)




def on_run_interval():
    """the board on the run's interval (it takes it from a check-in answer, up to one old interval later)"""
    t0 = time.time()
    while time.time() - t0 < 75 and board_checkin() != CHECKIN_S:
        time.sleep(2)
    check(f"the board checks in every {CHECKIN_S} s", board_checkin() == CHECKIN_S,
          f"check-in every {board_checkin()} s after {time.time() - t0:.0f} s")


class Stop(Exception):
    """a step failed: the later steps depend on it, so the run ends there (no waiting out their timeouts)"""


def wait_for(name, fn, secs):
    t0, got = time.time(), None
    while time.time() - t0 < secs:
        ok, got = fn()
        if ok:
            check(name, True, f"{time.time() - t0:.0f} s: {got}")
            return time.time() - t0
        time.sleep(1)
    check(name, False, f"not within {secs} s: {got}")
    raise Stop(name)


def applog():
    return adb("logcat", "-d", "-s", "LeakcamSetup:I")


def last(pattern):
    m = re.findall(pattern, applog())
    return m[-1] if m else None


def tap_setup_button():
    """the status screen's setup button (Wake / Change the Wi-Fi), from a UI dump (no instrumentation runs now); the
    app brought to the front again when another app took the screen (2026-11-03: Chrome opened by hand, the dump
    had no button)"""
    for attempt in (1, 2):
        adb("shell", "uiautomator", "dump", "/sdcard/t119.xml")
        xml = adb("shell", "cat", "/sdcard/t119.xml")
        # Wake / Change Wi-Fi is a rare action: the three-dots menu next to Take photo / Take video (2026-11-07)
        m = re.search(r'resource-id="com.leakcam.setup:id/more"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)
        if m:
            x1, y1, x2, y2 = map(int, m.groups())
            adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
            time.sleep(0.8)
            menu = adb("exec-out", "uiautomator", "dump", "/dev/tty", secs=30)
            i = re.search(r'text="(Wake to change Wi-Fi|Change Wi-Fi)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', menu)
            if i:
                x1, y1, x2, y2 = map(int, i.groups()[1:])
                adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
                return True
            adb("shell", "input", "keyevent", "KEYCODE_BACK")
        top = re.search(r'package="([^"]+)"', xml)
        r.obstacle(f"Wake tap {attempt}: the app not on the screen ({top.group(1) if top else 'no dump'} in front)")
        adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
        time.sleep(3)
    return False


def wake():
    """the server's wake for the board, as the app asks it: the HTTP status"""
    try:
        urllib.request.urlopen(urllib.request.Request(f"{SERVER}/v1/devices/{dev}/wake", method="POST"), timeout=5)
        return 200
    except urllib.error.HTTPError as e:
        return e.code


def add_only(dev):
    """the app's list (the server's for this phone, DESIGN.md 5.3.5) holds just this board, as a setup leaves it"""
    adb("shell", "am", "force-stop", PKG)
    only_boards(dev)


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if not product():
    r.obstacle("the BL616 runs the test firmware: flash the product (firmware/bl616_full, make flash)")
    r.finish("not run: not the product firmware.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
checkin_interval(CHECKIN_S)
try:
    # 0. present in the full firmware (no BLE); the app knows the board and shows it out of range with a Wake button
    if firmware() != "full":
        bl("slot switch", 1)                    # a stored network is assumed (test106 leaves the real one)
    wait_for("present: the full firmware", lambda: ((g := firmware()) == "full", g), 60)
    on_run_interval()
    if BATTERY:
        check("USB-C 1 out (hand)", hand("unplug USB-C 1 (the cell stays in)", "unplugged") is not None)
        time.sleep(5)
        wait_for("on battery: still the full firmware, present", lambda: ((g := firmware()) == "full" and online(dev), g),
                 30)
    add_only(dev)
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    wait_for("app: present, out of BLE range (Wi-Fi icon, no Bluetooth icon)",
             lambda: ((g := last(rf"row {dev} (wifi \d ble \d red \d)")) == "wifi 1 ble 0 red 0", g), 90)
    wait_for("app: the button wakes the board", lambda: ((g := last(r"button (.*) enabled (\d)")) is not None
                                                         and g[0].startswith("Wake") and g[1] == "1", g), 15)
    # 1. the tap: the server holds the wake, the board's next check-in switches it to the setup firmware
    t0 = time.time()
    ok = tap_setup_button()
    check("app: Wake tapped", ok)
    if not ok:
        raise Stop("app: Wake tapped")
    wait_for("app: the server took the wake", lambda: ((g := last(rf"wake {dev} answer (-?\d+)")) == "200", g), 10)
    if not BATTERY:                             # on battery asleep without a console: the app's icon below shows it
        wait_for("board: woken at its next check-in (the setup firmware)", lambda: ((g := firmware()) == "setup", g),
                 CHECKIN_S + 40)
    t_delivered = time.time()                   # the server's 2 minutes count from the check-in that took the wake
    wait_for("app: the board in range (Bluetooth icon)",
             lambda: ((g := last(rf"row {dev} (wifi \d ble \d red \d)")) == "wifi 1 ble 1 red 0", g), 40)
    wait_for("app: the button changes its Wi-Fi", lambda: ((g := last(r"button (.*) enabled (\d)")) is not None
                                                           and g[0].startswith("Change") and g[1] == "1", g), 15)
    if BATTERY:
        t_delivered = time.time()               # later than the delivery: the 2 minutes are surely over below
    t_ready = time.time() - t0
    # 2. the Wi-Fi changed from the app (BleOp taps the same button on the status screen): back to the full firmware
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=45)
    helps = res.get("help", [])
    end = helps[-1]["help"] if helps else res.get("error", "no help text")
    check("the Wi-Fi changed after the wake", end.startswith("Done."), f"{res.get('ms_end', 0) / 1000:.1f} s")
    if not end.startswith("Done."):
        raise Stop("the Wi-Fi changed after the wake")
    wait_for("changed: back in the full firmware", lambda: ((g := firmware()) == "full", g), 60)
    # 3. a second wake within 2 minutes: refused, the battery spared
    code = wake()
    check("server: a second wake within 2 minutes refused", code == 429, str(code))
    # 4. a wake nobody follows: the setup firmware for 2 minutes, then back by itself
    while time.time() - t_delivered < 125:
        time.sleep(1)
    code = wake()
    check("server: a wake after the 2 minutes taken", code == 200, str(code))
    if BATTERY:
        t_in = seen(dev, CHECKIN_S + 40)
        check("unanswered wake: the setup firmware (found by the phone)", t_in is not None,
              f"{t_in:.0f} s" if t_in is not None else "not found")
        if t_in is None:
            raise Stop("unanswered wake: the setup firmware")
    else:
        t_in = wait_for("unanswered wake: the setup firmware", lambda: ((g := firmware()) == "setup", g), CHECKIN_S + 40)
    t1 = time.time()
    wait_for("unanswered wake: back in the full firmware by itself", lambda: ((g := firmware()) == "full", g), 180)
    t_window = time.time() - t1
    low = 80 if BATTERY else 100                # on battery timed from the phone's find, up to one 20 s scan late
    check("unanswered wake: about 2 minutes in the setup firmware", t_in is not None and low <= t_window <= 160,
          f"{t_window:.0f} s")
    if BATTERY:
        check("USB-C 1 back in (hand)", hand("plug USB-C 1 back in", "plugged") is not None)
    r.set("speed", f"Wake tap -> the button changes the Wi-Fi {t_ready:.0f} s (check-in every {CHECKIN_S} s: up to one interval); "
                   f"an unanswered wake back in the full firmware after {t_window:.0f} s" + (" (on battery)" if BATTERY else ""))
    r.sample("after the checks")
except Stop as e:
    log("stopped at:", e)
finally:
    checkin_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
