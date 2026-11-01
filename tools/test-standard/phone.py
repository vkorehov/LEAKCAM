"""The Android bench phone for the BLE tests (test040, test106): adb, the instrumented BleOp (android/app/src/androidTest),
installs that answer Play Protect, and the phone made ready for a run (skill android-bench-phone)."""
import hashlib
import json
import os
import re
import subprocess
import tempfile
import threading
import zlib

from harness import REPO, bl   # harness first: it puts tools/power-bringup (board) on the path

APK = os.path.join(REPO, "android/app/build/outputs/apk/debug/app-debug.apk")
TEST_APK = os.path.join(REPO, "android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk")
ARGS_FILE = "/data/local/tmp/leakcam_bleop.json"
RUNNER = "com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner"


def adb(*a, secs=180):
    return subprocess.run(["adb", *a], capture_output=True, text=True, timeout=secs).stdout


def op(name, **kw):
    """one BleOp call; its JSON result (never printed whole: no values in it, but keep the log short). The arguments go
    in a file pushed to the phone and removed after the call: on the command line adbd logged them, the Wi-Fi
    passphrase in hex included, into the phone's logcat (2026-10-30)"""
    fd, local = tempfile.mkstemp(suffix=".json")          # mode 600
    try:
        with os.fdopen(fd, "w") as f:
            json.dump({"op": name, **{k: str(v) for k, v in kw.items()}}, f)
        adb("push", local, ARGS_FILE, secs=30)
        adb("shell", "chmod", "644", ARGS_FILE, secs=30)    # the test app reads it (another uid)
        out = adb("shell", "am", "instrument", "-w", "-r", "-e", "argsfile", ARGS_FILE, "-e", "op", name, RUNNER, secs=240)
    finally:
        os.unlink(local)
        adb("shell", "rm", "-f", ARGS_FILE, secs=30)
    m = re.search(r"INSTRUMENTATION_STATUS: result=(.*)", out)
    return json.loads(m.group(1)) if m else {"error": "no result from the phone"}


def product():
    """True on the BL616 product firmware (firmware/bl616: `prov`, BLE on by itself with USB power), False on the test
    firmware (bl616_powertest: `pt_ble`)"""
    # its own answer, not the help listing: since presence (test111) the Wi-Fi logs interleave and cut `help` short
    return "prov: USB power" in bl("prov", 2, r"prov: USB power") or "prov: USB power" in bl("prov", 2, r"prov: USB power")


def creds():
    """(ssid len, crc), (psk len, crc) as stored on the BL616 (`prov` / `pt_ble creds`, never the passphrase); None
    for a missing key"""
    out = bl("prov" if product() else "pt_ble creds", 2, r"wifi_psk[^\n]*\n")
    got = []
    for key in ("wifi_ssid", "wifi_psk"):
        m = re.search(key + r" len (\d+) crc ([0-9a-f]{8})", out)
        got.append((int(m.group(1)), int(m.group(2), 16)) if m else None)
    return tuple(got)


def want(ssid, psk):
    return tuple((len(v.encode()), zlib.crc32(v.encode())) if v is not None else None for v in (ssid, psk))


def install(apk, notes):
    """adb install, answering Play Protect's "verify this app" dialog on the phone (an install over USB waits for it):
    its "install without checking" button is tapped from a UI dump (2026-10-28, user: no hand on the phone); the test
    APK's dialog says "Не проверять" (2026-10-30)"""
    done = threading.Event()

    def press():
        while not done.wait(1):
            xml = adb("exec-out", "uiautomator", "dump", "/dev/tty", secs=20)
            if "com.android.vending" not in xml:
                continue
            m = re.search(r'text="[^"]*(?:без проверки|Не проверять|without (?:checking|scanning)|anyway|Не отправлять|Don.t send)[^"]*"'
                          r'[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml, re.I)
            if m:
                x1, y1, x2, y2 = map(int, m.groups())
                adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
                notes.append("Play Protect dialog answered at install")

    t = threading.Thread(target=press, daemon=True)
    t.start()
    adb("install", "-r", "-g", "-t", apk)       # -g: the Bluetooth permissions granted at install
    done.set()
    t.join()


def ready(notes):
    """the phone made ready for a run; None, or why it cannot run"""
    if "device" not in adb("get-state", secs=10):
        return "no Android phone on adb (USB debugging on, the prompt accepted)"
    for pkg, apk in (("com.leakcam.setup", APK), ("com.leakcam.setup.test", TEST_APK)):
        # only a changed APK is installed: every install over USB brings up Play Protect's "verify this app" (2026-10-28)
        on_phone = adb("shell", "pm", "path", pkg).strip().replace("package:", "")
        if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(apk, "rb").read()).hexdigest()]:
            install(apk, notes)
    adb("shell", "svc", "power", "stayon", "usb")   # the screen stays on while plugged: the pairing dialog shows
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "cmd", "bluetooth_manager", "enable")
    # the phone charges from the computer, not the other way round: a Samsung on a USB-C port came up as the power
    # source and ran its battery down to 3 % during test040 run 6 (2026-10-28)
    if re.search(r"status=\{[^}]*?power_role=source", adb("shell", "dumpsys", "usb"), re.S):
        adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "sink", "device")
        notes.append("phone USB port was the power source: swapped to sink")
    # a locked phone shows the pairing dialog over the lock screen, where UI Automator cannot press it (2026-10-28)
    if "isKeyguardShowing=true" in adb("shell", "dumpsys", "window"):
        return "the phone is locked: unlock it (screen lock off for the bench phone, or Developer options > Stay awake)"
    return None
