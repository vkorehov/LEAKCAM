#!/usr/bin/env python3
"""test119 automated: a phone wakes a present board's BLE through the server (user 2026-11-01: "stay notified by the
phone"). A configured, present board on battery does not advertise. The app shows a present board out of range with
a Wake button; the tap asks the server (POST /v1/devices/<id>/wake), the board's next check-in gets "ble":1 and it
advertises for 2 minutes (`prov`: "on (woken by a phone)"); the app finds it and its button turns into "Change the Wi-Fi";
the Wi-Fi is changed from the app. A second wake within 2 minutes is refused (429). Product firmware; `prov battery on`
makes it follow the battery rule on USB power. USB-C 1 in, the phone on adb, unlocked; nothing by hand.
    tests/test119-wake-through-server/run.py
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, board_id, op, product, ready   # noqa: E402

SERVER = "http://192.168.1.159:8090"
PI = "pi@192.168.1.159"
POLL_S = 20                      # for the run: the wake waits for the next check-in
PKG = "com.leakcam.setup"

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


def poll_interval(s):
    # only the poll line: the env file also holds the pairing PIN key (server/deploy.sh)
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, f"sed -i 's/^LEAKCAM_POLL_S=.*/LEAKCAM_POLL_S={s}/' "
                    "~/leakcam-server/env && sudo systemctl restart leakcam-server"], check=True, timeout=30)
    time.sleep(1)


def board_poll():
    """the board's check-in interval as it took it from the server's last answer"""
    m = re.search(r"poll (\d+) s", bl("presence", 2, r"poll \d+ s"))
    return int(m.group(1)) if m else 0


def on_run_interval():
    """the board on the run's interval (it takes it from a check-in answer, up to one old interval later)"""
    t0 = time.time()
    while time.time() - t0 < 75 and board_poll() != POLL_S:
        time.sleep(2)
    check(f"the board checks in every {POLL_S} s", board_poll() == POLL_S,
          f"poll {board_poll()} s after {time.time() - t0:.0f} s")


def rule():
    m = re.search(r"on battery (on \([^)]*\)|off \([^)]*\))", bl("prov", 2, r"on battery (on|off)"))
    return m.group(1) if m else "?"


def wait_for(name, fn, secs):
    t0, got = time.time(), None
    while time.time() - t0 < secs:
        ok, got = fn()
        if ok:
            check(name, True, f"{time.time() - t0:.0f} s: {got}")
            return time.time() - t0
        time.sleep(1)
    check(name, False, f"not within {secs} s: {got}")
    return None


def applog():
    return adb("logcat", "-d", "-s", "LeakcamSetup:I")


def last(pattern):
    m = re.findall(pattern, applog())
    return m[-1] if m else None


def tap_setup_button():
    """the status screen's setup button (Wake / Change the Wi-Fi), from a UI dump (no instrumentation runs now)"""
    adb("shell", "uiautomator", "dump", "/sdcard/t119.xml")
    xml = adb("shell", "cat", "/sdcard/t119.xml")
    m = re.search(r'resource-id="com.leakcam.setup:id/setup_open"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)
    if not m:
        return False
    x1, y1, x2, y2 = map(int, m.groups())
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
    return True


def add_only(dev):
    adb("shell", "am", "force-stop", PKG)
    body = json.dumps({dev: {}}).replace('"', "&quot;")
    new = f"<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n    <string name=\"devices\">{body}</string>\n</map>\n"
    subprocess.run(["adb", "shell", f"run-as {PKG} sh -c 'cat > shared_prefs/devices.xml'"], input=new, text=True,
                   check=True, timeout=30)


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if not product():
    r.obstacle("the BL616 runs the test firmware: flash the product (firmware/bl616, make flash)")
    r.finish("not run: not the product firmware.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
poll_interval(POLL_S)
try:
    on_run_interval()
    # 0. present, silent on battery; the app knows the board and shows it out of range with a Wake button
    wait_for("present: silent on battery", lambda: ((g := rule()).startswith("off (configured"), g), 2 * POLL_S + 20)
    bl("prov battery on", 2, r"battery rule")
    add_only(dev)
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    wait_for("app: present, out of BLE range (Wi-Fi icon, no Bluetooth icon)",
             lambda: ((g := last(rf"row {dev} (wifi \d ble \d red \d)")) == "wifi 1 ble 0 red 0", g), 90)
    wait_for("app: the button wakes the board", lambda: ((g := last(r"button (.*) enabled (\d)")) is not None
                                                         and g[0].startswith("Wake") and g[1] == "1", g), 15)
    # 1. the tap: the server holds the wake, the board's next check-in opens BLE for a minute
    t0 = time.time()
    check("app: Wake tapped", tap_setup_button())
    wait_for("app: the server took the wake", lambda: ((g := last(rf"wake {dev} answer (-?\d+)")) == "200", g), 10)
    t_woken = wait_for("board: woken at its next check-in", lambda: ((g := rule()).startswith("on (woken"), g),
                       POLL_S + 20)
    wait_for("app: the board in range (Bluetooth icon)",
             lambda: ((g := last(rf"row {dev} (wifi \d ble \d red \d)")) == "wifi 1 ble 1 red 0", g), 40)
    wait_for("app: the button changes its Wi-Fi", lambda: ((g := last(r"button (.*) enabled (\d)")) is not None
                                                           and g[0].startswith("Change") and g[1] == "1", g), 15)
    t_ready = time.time() - t0
    # 2. the Wi-Fi changed from the app (BleOp taps the same button on the status screen)
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=150)
    helps = res.get("help", [])
    end = helps[-1]["help"] if helps else res.get("error", "no help text")
    check("the Wi-Fi changed after the wake", end.startswith("Done."), f"{res.get('ms_end', 0) / 1000:.1f} s")
    # 3. a second wake within 2 minutes: refused, the battery spared
    try:
        urllib.request.urlopen(urllib.request.Request(f"{SERVER}/v1/devices/{dev}/wake", method="POST"), timeout=5)
        code = 200
    except urllib.error.HTTPError as e:
        code = e.code
    check("server: a second wake within 2 minutes refused", code == 429, str(code))
    # 4. the 2 minutes over: silent again
    wait_for("board: silent again after the wake window", lambda: ((g := rule()).startswith("off (configured"), g), 150)
    r.set("speed", f"Wake tap -> the button changes the Wi-Fi {t_ready:.0f} s (poll {POLL_S} s: up to one interval)")
    r.sample("after the checks")
finally:
    bl("prov battery off", 2)
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
