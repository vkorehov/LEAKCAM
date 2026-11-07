#!/usr/bin/env python3
"""test129: the app shows the board's power and signal as icons (user 2026-11-04, DESIGN.md 5.3.4). On the product pair:
a board with no network sits in the setup firmware on USB power and advertises flags bit 1 -> the app's row shows a plug
icon; the signal as 1-4 bars, never the dB value; a long press on the bars shows it ("dbm <n>" in the log). The app
logs every redraw of a row ("row <id> ... power plug|battery|- bars <n> rssi <dBm>"); the runner reads those. At the
end the real network is set from the app again (back to the full firmware).
    tests/test129-app-power-signal-icons/run.py [--battery]   --battery: USB-C 1 out (hand step) -> a battery icon,
                                                                 then back in (hand step)
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, board_id, creds, firmware, hand, only_boards, op, ready, to_firmware, want   # noqa: E402

PKG = "com.leakcam.setup"
BARS = ((-60, 4), (-70, 3), (-80, 2))           # the app's barCount(): >= -60 4 bars, >= -70 3, >= -80 2, else 1

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test129: another run.py is running")

r = Run(__file__)
checks = []


class Stop(Exception):
    """a step failed: the later steps depend on it"""


def check(name, ok, detail="", stop=False):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)
    if stop and not ok:
        raise Stop(name)


def bars_for(dbm):
    return next((n for floor, n in BARS if dbm >= floor), 1)


def row(dev):
    """the setup list's row for the board as the app last logged it: (power, bars, rssi) or None"""
    m = re.findall(rf"setup row {dev} power (\S+) bars (\d) rssi (-?\d+)",
                   adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=30))
    return (m[-1][0], int(m[-1][1]), int(m[-1][2])) if m else None


def wait_power(dev, name, power, secs):
    t0, got = time.time(), None
    while time.time() - t0 < secs:
        got = row(dev)
        if got and got[0] == power:
            check(name, True, f"{time.time() - t0:.0f} s, row {got}")
            return got
        time.sleep(1)
    check(name, False, f"not within {secs} s, row {got}", stop=True)


def long_press_signal(dev):
    """a long press on the board's setup row (its icons are one drawable after the serial); the dB value it logged"""
    for attempt in (1, 2):
        xml = adb("exec-out", "uiautomator", "dump", "/dev/tty", secs=30)
        m = re.search(rf'text="{dev}"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)
        if not m:
            log("long press", attempt, "no row for the board in the screen dump", f"({len(xml)} B)")
            time.sleep(2)
            continue
        x1, y1, x2, y2 = map(int, m.groups())
        x, y = str((x1 + x2) // 2), str((y1 + y2) // 2)
        adb("logcat", "-c")
        adb("shell", "input", "swipe", x, y, x, y, "800", secs=15)
        time.sleep(1)
        app = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=30)
        got = re.findall(r"dbm (-?\d+)", app)
        if got:
            return int(got[-1])
        log("long press", attempt, f"at {x},{y}: no dbm line")
    return None


ssid, psk = wifi_credentials()
BATTERY = "--battery" in sys.argv
if not os.path.exists(BL_PORT) or firmware() not in ("full", "setup"):
    r.obstacle(f"the product pair is needed (the board runs {firmware()})")
    r.finish("not run: not the product pair.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
try:
    # 1. no network: the setup firmware on USB power, advertising flags bit 1
    bl("provision erase", 2, r"erased|erase failed")
    t = to_firmware("setup", secs=90)   # the full firmware sees the erase at its next check-in (31 s measured, test116)
    check("no network: the setup firmware on USB", t is not None, f"{t:.0f} s" if t else str(firmware()), stop=True)
    # a new device for this phone: the app opens on the setup screen, where the board's row shows its power and
    # signal (the status screen has no Bluetooth since 2026-11-07, DESIGN.md 5.3.4)
    adb("shell", "am", "force-stop", PKG)
    only_boards()
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    # 2. the plug icon and the bars matching the signal
    got = wait_power(dev, "USB power: the plug icon", "plug", 60)
    check("signal as bars, matching the dB value", got[1] == bars_for(got[2]), f"{got[1]} bars at {got[2]} dBm")
    # 3. the long press shows the dB value
    dbm = long_press_signal(dev)
    check("long press on the bars: the dB value", dbm is not None and -110 < dbm < 0, f"{dbm} dBm")
    # 4. on battery: the battery icon (hand steps)
    if BATTERY:
        if hand("unplug USB-C 1 (the cell stays in)", "unplugged") is None:
            raise Stop("no hand step: unplug")
        wait_power(dev, "on battery: the battery icon", "battery", 90)
        if hand("plug USB-C 1 back in", "plugged") is None:
            raise Stop("no hand step: plug")
    r.sample("after the checks")
except Stop as e:
    log("stopped at:", e)
finally:
    # the real network back from the app: the full firmware again (as the bench is left)
    adb("shell", "am", "force-stop", PKG)
    only_boards()
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=45)
    t = to_firmware("full", secs=30)
    check("end: the real network set again, the full firmware", t is not None and creds() == want(ssid, psk),
          "full" if t else str(firmware()))

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
