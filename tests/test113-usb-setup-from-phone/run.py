#!/usr/bin/env python3
"""test113: full setup of a LEAKCAM with no network stored, by the LEAKCAM Setup app on the bench phone over a USB-C
cable into USB-C 1 (phone = USB host), product firmware on both chips, BLE not used (DESIGN.md 5.3.4, 5.3.8). The
network is erased first (`provision erase`); hand steps move USB-C 1 to the phone and back. The app's own screens do
the setup (BleOp `app`: the setup button, the Wi-Fi fields from a pushed file, Send); Android's one-time "open LEAKCAM
Setup for this USB device" dialog is answered "always" from a UI dump. Never prints the network or the PIN.
    tests/test113-usb-setup-from-phone/run.py
"""
import os
import re
import sys
import time
import urllib.request
import json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import log, wifi_credentials   # noqa: E402
from phone import SERVER, adb, board_id, creds, firmware, hand, op, ready, want   # noqa: E402

checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def usb_screen():
    """the app, launched by the plug, has the board on the cable and its screen open: its log line `usb screen <id>`
    (dumpsys usb's history lists every attach, also ones that dropped at once: 2026-11-05)"""
    m = re.search(r"usb screen (\w+) creds (\d)", adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15))
    return m.groups() if m else None


def usb_dialog():
    """Android's USB dialog for the app (open it for this device / allow access): 'always' ticked, OK; True if one was
    answered. Only by resource id and class: the phone's UI is Russian"""
    xml = adb("exec-out", "uiautomator", "dump", "/dev/tty", secs=20)
    if "com.leakcam.setup" not in xml or 'resource-id="android:id/button1"' not in xml:
        return False
    for node in re.findall(r"<node [^>]*>", xml):
        if 'class="android.widget.CheckBox"' in node and 'checked="false"' in node:
            tap(node)
        if 'resource-id="android:id/button1"' in node:
            ok = node
    tap(ok)
    return True


def tap(node):
    x1, y1, x2, y2 = map(int, re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', node).groups())
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def online(dev):
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
            return json.loads(f.read()).get("online") is True
    except OSError:
        return False


r = Run(__file__)
notes = []
why = ready(notes)
if why:
    r.finish(f"not run: {why}", False)
    sys.exit(1)
ssid, psk = wifi_credentials()
dev = board_id()

# 1. a board with no network: erased, back to the setup firmware
log("board: network erased")
bl("provision erase", 2)
bl("reboot", 1)
t = time.time()
while firmware() != "setup" and time.time() - t < 60:
    time.sleep(2)
check("no network stored: the setup firmware runs", firmware() == "setup" and creds()[0] is None, firmware() or "?")

# 2. the cable to the phone: the plug launches the app (default handler), which opens the cable screen
adb("shell", "am", "force-stop", "com.leakcam.setup")
adb("logcat", "-c")
if hand("USB-C 1: take the cable off the bench, plug the phone in with a USB-C cable", "phone") is None:
    r.finish("not run: no hand step (phone not plugged)", False)
    sys.exit(1)
t_plug = time.time()
seen = None
dialogs = 0
screen = None
while time.time() - t_plug < 30:
    if seen is None:
        screen = usb_screen()
        if screen:
            seen = time.time() - t_plug
    if usb_dialog():
        dialogs += 1
        log("phone: the USB dialog answered 'always'")
    if seen is not None and time.time() - t_plug > seen + 3:
        break
    time.sleep(0.3)
check("the plug opens the app on the cable screen of this board, no network", screen == (dev, "0"),
      f"{seen:.2f} s after the hand step" if seen is not None else "not within 30 s")
if dialogs:
    notes.append(f"Android's USB dialog answered {dialogs} time(s) ('always' ticked: not again on this phone)")

# 3. the app does the setup over the cable
res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), secs=150)
check("the app's setup ran on the cable screen (no list, no Find)", res.get("usb_screen") is True)
helps = [h["help"] for h in res.get("help", [])]
last = helps[-1] if helps else res.get("error", "no help text")
log("app:", res.get("error", ""), " | ".join(h[:70].replace("\n", " ").replace(ssid, "<network>") for h in helps[-4:]))
check("the app used the cable (no Bluetooth pairing)", any("over the USB cable" in h for h in helps) and
      not any("pair with" in h for h in helps), f"{len(helps)} screens")
check("the app says Done", last.startswith("Done"), last[:60].replace(ssid, "<network>"))
secs = res.get("ms_end", 0) / 1000
if secs:
    r.set("speed", (f"plug -> the phone lists the board {seen:.2f} s; " if seen is not None else "") +
          f"app start -> Done {secs:.1f} s over the cable")
r.set("boot", "n/a in this run: the K230 boot is timed by test045 (READY 0.43 s after pt_run)")
r.set("share", "n/a: the setup session's K230 boot, see test045")
r.set("current", "n/a: on the phone's USB power, not metered")

# 4. the board: network stored, the full firmware, present at the server
t = time.time()
while firmware() != "full" and time.time() - t < 60:
    time.sleep(2)
check("the network stored on the board", creds() == want(ssid, psk))
check("the board back on the full firmware", firmware() == "full", firmware() or "?")
t = time.time()
while not online(dev) and time.time() - t < 120:
    time.sleep(3)
check("the server sees the board online", online(dev), f"{time.time() - t:.0f} s")

hand("USB-C 1: take the phone off, plug the bench cable back in", "bench")
r.sample("after the setup")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
