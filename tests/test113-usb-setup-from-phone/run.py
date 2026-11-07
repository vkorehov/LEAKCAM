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
import threading
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


def phone_host():
    """the phone is USB host on its port: a cable to the board is in"""
    return "host_connected=true" in adb("shell", "dumpsys", "usb", secs=10)


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
while firmware() != "setup" and time.time() - t < 30:
    time.sleep(2)
check("no network stored: the setup firmware runs", firmware() == "setup" and creds()[0] is None, firmware() or "?")

# 2. the cable to the phone: the plug launches the app (default handler), which opens the cable screen
adb("shell", "am", "force-stop", "com.leakcam.setup")
adb("logcat", "-c")
log("ACTION: USB-C 1: take the cable off the bench, plug the phone in with a USB-C cable (detected on the phone)")
t = time.time()
while phone_host():                          # still on the phone from before: wait for the unplug first
    if time.time() - t > 120:
        r.finish("not run: the phone was never unplugged", False)
        sys.exit(1)
    time.sleep(1)
while not phone_host():                      # the plug: the phone becomes USB host (a hand file was mixed up, 2026-11-06)
    if time.time() - t > 120:
        r.finish("not run: no plug into the phone within 120 s", False)
        sys.exit(1)
    time.sleep(0.5)
t_plug = time.time()
seen = None
dialogs = 0
screen = None
while time.time() - t_plug < 15:
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
      f"{seen:.2f} s after the plug" if seen is not None else "not within 15 s")
if dialogs:
    notes.append(f"Android's USB dialog answered {dialogs} time(s) ('always' ticked: not again on this phone)")

def setup(psk_text, pull=False, secs=30):
    """the app's setup on the cable screen; pull: the phone's port switched to sink/device (VBUS and the USB link gone,
    as a pulled cable) once the app shows the join check, back to source/host 3 s later"""
    stop = []
    if pull:
        def puller():
            t0 = time.time()
            while not stop and time.time() - t0 < 30:
                if "now joining" in adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=10):
                    adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "sink", "device", secs=10)
                    log("phone: the cable 'pulled' (port to sink/device) during the join check")
                    time.sleep(3)
                    adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "source", "host", secs=10)
                    log("phone: the cable back (port to source/host)")
                    return
                time.sleep(0.2)
        th = threading.Thread(target=puller)
        adb("logcat", "-c")
        th.start()
    res = op("app", ssid=ssid.encode().hex(), psk=psk_text.encode().hex(), secs=secs)
    if pull:
        stop.append(1)
        th.join()
    helps = [h["help"] for h in res.get("help", [])]
    last = helps[-1] if helps else res.get("error", "no help text")
    log("app:", res.get("error", ""), " | ".join(h[:70].replace("\n", " ").replace(ssid, "<network>") for h in helps[-4:]))
    return res, helps, last


# 7. a wrong password over the cable: named, nothing stored, the cable screen stays for the retry
res, helps, last = setup(psk + "x")
check("a wrong password is named over the cable", "refused the password" in last and
      not any("pair with" in h for h in helps), last[:60].replace(ssid, "<network>"))
check("a wrong password stores nothing: still the setup firmware", creds()[0] is None and firmware() == "setup",
      firmware() or "?")

# 3. the app does the setup over the cable
res, helps, last = setup(psk)
check("the app's setup ran on the cable screen (no list, no Find)", res.get("usb_screen") is True)
# the cable screen's own text; "Sending ... over the USB cable" lasts milliseconds (answers in 1-3 ms), never seen
check("the app used the cable (no Bluetooth pairing)", any("Check and save" in h for h in helps) and
      not any("pair with" in h for h in helps), f"{len(helps)} screens")
check("the app says Done", last.startswith("Done"), last[:60].replace(ssid, "<network>"))
secs = res.get("ms_end", 0) / 1000
speed = ""
if secs:
    speed = ((f"plug -> the phone lists the board {seen:.2f} s; " if seen is not None else "") +
             f"app start -> Done {secs:.1f} s over the cable")
    r.set("speed", speed)
r.set("boot", "n/a in this run: the K230 boot is timed by test045 (READY 0.43 s after pt_run)")
r.set("share", "n/a: the setup session's K230 boot, see test045")
r.set("current", "n/a: on the phone's USB power, not metered")

# 4. the board: network stored, the full firmware, present at the server
t = time.time()
while firmware() != "full" and time.time() - t < 30:
    time.sleep(2)
check("the network stored on the board", creds() == want(ssid, psk))
check("the board back on the full firmware", firmware() == "full", firmware() or "?")
t = time.time()
while not online(dev) and time.time() - t < 30:
    time.sleep(3)
check("the server sees the board online", online(dev), f"{time.time() - t:.0f} s")

# 8. the cable pulled during the join check: the app claims nothing, the board finishes the check by itself
log("board: network erased again, the phone stays plugged")
adb("logcat", "-c")
bl("provision erase", 2)
bl("reboot", 1)
t = time.time()
while usb_screen() is None and time.time() - t < 30:     # the K230 back on the cable: the plug opens the app
    time.sleep(1)
check("erased with the phone in: the cable screen again", usb_screen() == (dev, "0"), f"{time.time() - t:.1f} s")
res, helps, last = setup(psk, pull=True, secs=15)
# from the app's log: the error shows only a moment, the app then lists the board (the driver's screen samples missed
# it, 2026-11-06)
applog = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15)
check("the cable pulled during the check: the app does not claim Done", "unplugged before it confirmed" in applog
      and "Done." not in applog, "error shown, no Done" if "unplugged before it confirmed" in applog else "no error")
t = time.time()
while (firmware() != "full" or not online(dev)) and time.time() - t < 30:
    time.sleep(2)
check("the board finished the check alone: network stored, online", creds() == want(ssid, psk) and online(dev),
      f"{firmware() or '?'}, {time.time() - t:.0f} s")

# 12. Change Wi-Fi on the set-up board (DESIGN.md 5.3.4): the full firmware's relay answers ID ... full; the app
# sends PIN + SETUP, rides out the switch and sends the setup firmware's lines; one Send for the user
adb("shell", "am", "force-stop", "com.leakcam.setup")
adb("logcat", "-c")
res, helps, last = setup(psk, secs=45)
applog = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15)
check("change Wi-Fi on the full firmware: the cable screen of the set-up board",
      res.get("usb_screen") is True and f"usb screen {dev} creds 1" in applog)
check("change Wi-Fi: the app switched the board to its setup firmware over the cable",
      "switching to its setup firmware" in applog and "on its setup firmware" in applog)
check("change Wi-Fi: the app says Done", last.startswith("Done"), last[:60].replace(ssid, "<network>"))
if res.get("ms_end"):
    r.set("speed", speed + f"; change Wi-Fi on the full firmware: app start -> Done {res['ms_end'] / 1000:.1f} s")
t = time.time()
while (firmware() != "full" or not online(dev)) and time.time() - t < 30:
    time.sleep(2)
check("change Wi-Fi: network stored, back on the full firmware, online",
      creds() == want(ssid, psk) and firmware() == "full" and online(dev), f"{firmware() or '?'}, {time.time() - t:.0f} s")
time.sleep(8)                                # the full firmware's relay back on the cable: Done stays, no cable screen
after = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15).split("on its setup firmware")[-1]
check("change Wi-Fi: Done stays when the board's relay comes back", "usb screen" not in after)

hand("USB-C 1: take the phone off, plug the bench cable back in", "bench")
r.sample("after the setup")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
