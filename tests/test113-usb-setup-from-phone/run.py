#!/usr/bin/env python3
"""test113: full setup of a LEAKCAM with no network stored, by the LEAKCAM Setup app on the bench phone over a USB-C
cable into USB-C 1 (phone = USB host), product firmware on both chips, BLE not used (DESIGN.md 5.3.4, 5.3.8). The
network is erased first (`provision erase`); the relay box moves USB-C 1 to the phone and back (no hands). The app's own screens do
the setup (BleOp `app`: the setup screen, the Wi-Fi fields from a pushed file, Send); Android's one-time "open LEAKCAM
Setup for this USB device" dialog is answered "always" from a UI dump. Setup lists new boards only (DESIGN.md 5.3.4):
the board comes off the phone's list before each setup, and once set up from this phone the cable no longer lists it.
Never prints the network or the PIN.
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
from phone import SERVER, adb, board_id, creds, erase, firmware, hand, only_boards, op, phone_boards, ready, screen_xml, want   # noqa: E402

checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def usb_screen():
    """the app, launched by the plug, on its setup screen with the board answering on the cable: `screen setup` and
    `usb board <id> creds <0|1>` in its log (dumpsys usb's history lists every attach, also ones that dropped at once:
    2026-11-05)"""
    applog = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15)
    m = re.search(r"usb board (\w+) creds (\d)", applog)
    return m.groups() if m and "screen setup" in applog else None


def listed(dev):
    """the setup screen lists dev (its row's text is the serial alone)"""
    return f'text="{dev}"' in screen_xml()


def phone_host():
    """the phone is USB host on its port: a cable to the board is in"""
    return "host_connected=true" in adb("shell", "dumpsys", "usb", secs=10)


def replug_roles():
    """a re-plug by the phone's port roles: sink/device (the board sees its USB power and link go), back to
    source/host as soon as the phone reports the host link gone (no fixed 3 s, user 2026-11-09)"""
    adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "sink", "device", secs=10)
    t0 = time.time()
    while time.time() - t0 < 5 and phone_host():
        time.sleep(0.2)
    adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "source", "host", secs=10)


def usb_dialog():
    """Android's USB dialog for the app (open it for this device / allow access): 'always' ticked, OK; True if one was
    answered. Only by resource id and class: the phone's UI is Russian"""
    xml = screen_xml()            # the setup screen's spinner: uiautomator dump took 10 s here
    # the dialog itself, by its "always" box (AOSP alwaysUse, Samsung sec_alwaysUse): not by the app on screen - over
    # the Settings screen it went unanswered and test113 stalled (2026-11-09, after the app's reinstall reset "always")
    if not re.search(r'resource-id="android:id/(sec_)?alwaysUse"', xml) or 'resource-id="android:id/button1"' not in xml:
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
erase()
bl("reboot", 1)
t = time.time()
while firmware() != "setup" and time.time() - t < 30:
    time.sleep(0.3)
check("no network stored: the setup firmware runs", firmware() == "setup" and creds()[0] is None, firmware() or "?")

# 2. the cable to the phone: the plug launches the app (default handler), which opens the setup screen
only_boards()
adb("shell", "am", "force-stop", "com.leakcam.setup")
adb("logcat", "-c")
t = time.time()
if phone_host():                             # already on the phone: a re-plug by the port's roles, no hands
    log("phone: already on USB-C 1, re-plugged by its port roles")
    replug_roles()
else:   # the relay box moves USB-C 1 to the phone (skill leakcam-bench-relays), else a hand step
    hand("USB-C 1: take the cable off the bench, plug the phone in with a USB-C cable (detected on the phone)", "phone")
while not phone_host():                      # the plug: the phone becomes USB host (a hand file was mixed up, 2026-11-06)
    if time.time() - t > 120:
        r.finish("not run: no plug into the phone within 120 s", False)
        sys.exit(1)
    time.sleep(0.5)
t_plug = time.time()
# the PC side of the move: USB-C 1 gone from the bench, the consoles stay; the plug's power change re-enumerates the
# BL616 console for a moment (gone 1 s after the plug: taken for the wrong cable, 2026-11-08)
t_con = time.time()
while time.time() - t_con < 10 and not os.path.exists("/dev/leakcam-bl616"):
    time.sleep(0.1)
while time.time() - t_con < 3 and (os.path.exists("/dev/leakcam-k230usb") or not os.path.exists("/dev/leakcam-bl616")):
    time.sleep(0.1)                           # USB-C 1 leaving the bench (no fixed 1 s)
if os.path.exists("/dev/leakcam-k230usb") or not os.path.exists("/dev/leakcam-bl616"):
    log("ACTION: wrong cable: the phone is on the BL616 console (USB-C 2); it belongs in USB-C 1 (the K230, "
        "/dev/leakcam-k230usb on the bench)")
    r.finish("not run: the phone on the wrong cable (the BL616 console, not USB-C 1)", False)
    sys.exit(1)
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
    if seen is not None and time.time() - t_plug > seen + 1:   # a USB dialog comes with the screen, not later
        break
    time.sleep(0.3)
check("the plug opens the app's setup screen on this board, no network", screen == (dev, "0"),
      f"{seen:.2f} s after the plug" if seen is not None else "not within 15 s")
if dialogs:
    notes.append(f"Android's USB dialog answered {dialogs} time(s) ('always' ticked: not again on this phone)")

def setup(psk_text, pull=False, secs=30, hold=0):
    """the app's setup with the board on the cable; pull: the phone's port switched to sink/device (VBUS and the USB link gone,
    as a pulled cable) once the app shows the join check, back to source/host 3 s later"""
    stop = []
    if pull:
        def puller():
            t0 = time.time()
            while not stop and time.time() - t0 < 30:
                if "now joining" in adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=10):
                    replug_roles()
                    log("phone: the cable 'pulled' (port to sink/device) during the join check")
                    log("phone: the cable back (port to source/host)")
                    return
                time.sleep(0.2)
        th = threading.Thread(target=puller)
        adb("logcat", "-c")
        th.start()
    res = op("app", ssid=ssid.encode().hex(), psk=psk_text.encode().hex(), secs=secs + hold, hold=hold)
    if pull:
        stop.append(1)
        th.join()
    helps = [h["help"] for h in res.get("help", [])]
    last = helps[-1] if helps else res.get("error", "no help text")
    log("app:", res.get("error", ""), " | ".join(h[:70].replace("\n", " ").replace(ssid, "<network>") for h in helps[-4:]))
    return res, helps, last


# 7. a wrong password over the cable: named, nothing stored, the setup screen stays for the retry
res, helps, last = setup(psk + "x")
check("a wrong password is named over the cable", "refused the password" in last and
      not any("pair with" in h for h in helps), last[:60].replace(ssid, "<network>"))
check("a wrong password stores nothing: still the setup firmware", creds()[0] is None and firmware() == "setup",
      firmware() or "?")
t = time.time()  # the app's DELETE runs beside the error: polled, not a fixed 1 s
while time.time() - t < 5 and dev in phone_boards():
    time.sleep(0.2)
check("a failed setup leaves the board off this phone's list (Setup lists it for the retry)", dev not in phone_boards())

# 3. the app does the setup over the cable
res, helps, last = setup(psk)
# the cable row's own help (help_usb_wifi); "Sending ... over the USB cable" lasts milliseconds (answers in 1-3 ms)
check("the app used the cable (no Bluetooth pairing)", any("should use (2.4 GHz;" in h for h in helps) and
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
    time.sleep(0.3)
check("the network stored on the board", creds() == want(ssid, psk))
check("the board back on the full firmware", firmware() == "full", firmware() or "?")
t = time.time()
while not online(dev) and time.time() - t < 30:
    time.sleep(0.5)
check("the server sees the board online", online(dev), f"{time.time() - t:.0f} s")

# 8. the cable pulled during the join check: the app claims nothing, the board finishes the check by itself
log("board: network erased again, the phone stays plugged")
only_boards()   # Done put it on the phone's list: off again for this setup
adb("shell", "am", "force-stop", "com.leakcam.setup")   # its list read afresh: the board's re-plug launches it
adb("logcat", "-c")
erase()
bl("reboot", 1)
t = time.time()
while usb_screen() is None and time.time() - t < 30:     # the K230 back on the cable: the plug opens the app
    time.sleep(0.3)
check("erased with the phone in: the setup screen with the board again", usb_screen() == (dev, "0"), f"{time.time() - t:.1f} s")
res, helps, last = setup(psk, pull=True, secs=15)
# from the app's log: the error shows only a moment, the app then lists the board (the driver's screen samples missed
# it, 2026-11-06)
applog = adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=15)
check("the cable pulled during the check: the app does not claim Done", "unplugged before it confirmed" in applog
      and "Done." not in applog, "error shown, no Done" if "unplugged before it confirmed" in applog else "no error")
t = time.time()
while (firmware() != "full" or not online(dev)) and time.time() - t < 30:
    time.sleep(0.3)
check("the board finished the check alone: network stored, online", creds() == want(ssid, psk) and online(dev),
      f"{firmware() or '?'}, {time.time() - t:.0f} s")

# 12. a board set up from this phone, on the cable: Setup skips it (DESIGN.md 5.3.4, user 2026-11-07: Setup is for new
# devices only; its Wi-Fi changes through the server, Change Wi-Fi). The port's roles flipped as a re-plug: the plug
# opens Setup, the board answers on the cable, no row for it
adb("shell", "am", "force-stop", "com.leakcam.setup")
adb("logcat", "-c")
adb("shell", "monkey", "-p", "com.leakcam.setup", "1", secs=15)    # on the status screen: its board is there
t = time.time()
while time.time() - t < 10 and "screen status" not in adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=10):
    time.sleep(0.2)
adb("logcat", "-c")
replug_roles()
t = time.time()
while time.time() - t < 15 and usb_screen() is None:
    usb_dialog()
    time.sleep(0.5)
answered = usb_screen()
check("a set-up board on the cable: the plug opens Setup, the board answers", answered == (dev, "1"),
      f"{time.time() - t:.1f} s" if answered else "no `usb board` line within 15 s")
check("a set-up board on the cable: Setup does not list it", answered is not None and not listed(dev))

hand("USB-C 1: take the phone off, plug the bench cable back in", "bench")
r.sample("after the setup")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed)
sys.exit(1 if failed else 0)
