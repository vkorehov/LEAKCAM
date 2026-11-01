#!/usr/bin/env python3
"""test111 automated: more than one LEAKCAM (use case 2) on the product firmware. One device ID on every channel (the
board's `presence`, its USB serial number, the BLE advertisement the app read); the board checks in at the presence
server (server/, the Pi) after a setup; the app remembers it and shows it online; a second LEAKCAM (an ID put into the
app's list, checked in from here) shows online, then offline when it stops. The server's poll interval is set short for
the run and put back. Needs the product firmware, USB-C 1 in, the phone on adb, unlocked.
    tests/test111-multi-device/run.py
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, op, product, ready   # noqa: E402

SERVER = "http://192.168.1.159:8090"
PI = "pi@192.168.1.159"
POLL_S = 10                      # for the run; the server's default is 60
SECOND = "02AB12CD34EF"          # the second LEAKCAM: an ID no board has (locally administered MAC)
PKG = "com.leakcam.setup"

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test111: another run.py is running")

r = Run(__file__)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def server(path):
    with urllib.request.urlopen(SERVER + path, timeout=5) as f:
        return json.loads(f.read())


def poll_interval(s):
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, f"echo LEAKCAM_POLL_S={s} > ~/leakcam-server/env && "
                    "sudo systemctl restart leakcam-server"], check=True, timeout=30)
    time.sleep(1)


def app_mine():
    """the app reopened: its presence lines from logcat ("mine <id>: <line>"), per ID"""
    adb("logcat", "-c")
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    time.sleep(6)
    out = adb("logcat", "-d", "-s", "LeakcamSetup:I")
    return dict(re.findall(r"mine ([0-9A-F]{12}): (.*)", out))


def add_to_app(dev_id, name):
    """an entry in the app's list as a second phone setup would leave it (debug build: run-as writes its prefs)"""
    adb("shell", "am", "force-stop", PKG)
    xml = adb("shell", "run-as", PKG, "cat", "shared_prefs/devices.xml")
    m = re.search(r'<string name="devices">(.*)</string>', xml, re.S)
    devices = json.loads(m.group(1).replace("&quot;", '"')) if m else {}
    devices[dev_id] = {"name": name, "ssid": "t111"}
    body = json.dumps(devices).replace('"', "&quot;")
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
poll_interval(POLL_S)
try:
    # 1. one device ID: the board, its USB serial number, the BLE advertisement (read by the app during a setup)
    # a command typed while the Wi-Fi stack logs can come back "not found" (2026-11-01): one retry
    m = re.search(r"presence: id ([0-9A-F]{12})", bl("presence", 2)) or \
        re.search(r"presence: id ([0-9A-F]{12})", bl("presence", 2))
    dev = m.group(1) if m else None
    usb = subprocess.run(["udevadm", "info", "-q", "property", BL_PORT], capture_output=True, text=True).stdout
    usb_id = (re.search(r"ID_SERIAL_SHORT=(\S+)", usb) or [None, None])[1]
    check("the board has a device ID", dev is not None, str(dev))
    check("USB serial number = the device ID", usb_id == dev, f"USB {usb_id}")
    adb("logcat", "-c")
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    check("setup Done", last.startswith("Done."), f"{res.get('ms_end', 0) / 1000:.1f} s")
    found = re.findall(r"found \S+ id ([0-9A-F]{12}|none)", adb("logcat", "-d", "-s", "LeakcamSetup:I"))
    check("BLE advertisement = the device ID", dev in found, str(found))

    # 2. the board checks in at the server: online within two intervals after the setup (the phone left)
    t0, online = time.time(), False
    while time.time() - t0 < 2 * 60 + 10 and not online:   # its first interval is the default 60 s until told
        online = server(f"/v1/devices/{dev}").get("online", False)
        if not online:
            time.sleep(3)
    check("server: the board online after the setup", online, f"{time.time() - t0:.0f} s")
    r.set("speed", f"setup Done -> online at the server {time.time() - t0:.0f} s (poll {POLL_S} s, the board's first "
          f"interval {60} s until told)")
    st = bl("presence", 2)
    check("board: last check-in reached the server (0x03)", "state 0x03" in st, st.strip().splitlines()[-1] if st else "")

    # 3. the app remembers it and shows it online
    lines = app_mine()
    check("app: the board listed and online", lines.get(dev, "").endswith("online"), lines.get(dev))

    # 4. a second LEAKCAM: listed, online while it checks in, offline when it stops
    add_to_app(SECOND, "LEAKCAM-SECOND")
    lines = app_mine()
    check("app: second LEAKCAM listed, not seen yet", "not seen yet" in lines.get(SECOND, ""), lines.get(SECOND))
    server(f"/v1/poll?id={SECOND}")
    lines = app_mine()
    check("app: second LEAKCAM online after its check-in", lines.get(SECOND, "").endswith("online"), lines.get(SECOND))
    check("app: the board still online next to it", lines.get(dev, "").endswith("online"), lines.get(dev))
    time.sleep(3 * POLL_S + 2)                               # three check-ins missed
    lines = app_mine()
    check("app: second LEAKCAM offline when it stopped", "offline" in lines.get(SECOND, ""), lines.get(SECOND))
    check("app: the board online throughout", lines.get(dev, "").endswith("online"), lines.get(dev))
    r.sample("after the checks")
finally:
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
