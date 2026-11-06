"""The Android bench phone for the BLE tests (test040, test106): adb over Wi-Fi, the instrumented BleOp
(android/app/src/androidTest), installs that answer Play Protect, and the phone made ready for a run (skill
android-bench-phone)."""
import hashlib
import json
import os
import re
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path

from harness import REPO, bl   # harness first: it puts tools/power-bringup (board) on the path

APK = os.path.join(REPO, "android/app/build/outputs/apk/debug/app-debug.apk")
TEST_APK = os.path.join(REPO, "android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk")
ARGS_FILE = "/data/local/tmp/leakcam_bleop.json"
RUNNER = "com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner"
PHONE_FILE = os.path.expanduser("~/.leakcam_phone")   # "<ip>:<port>" of the phone's Wireless debugging (adb over Wi-Fi)
# the phone's two addresses: 192.168.1.163 on one home network, 192.168.1.141 on "VK" (user, 2026-11-05); the port is
# the file's, tried on both
PHONE_IPS = ("192.168.1.141", "192.168.1.163")
SERVER = "http://192.168.1.159:8090"   # the presence server (server/, the Pi): the board's pairing PIN comes from it


def board_id():
    """the board's serial (device ID) from its USB serial number; "" when the console is not there"""
    from board import BL_PORT
    usb = subprocess.run(["udevadm", "info", "-q", "property", BL_PORT], capture_output=True, text=True).stdout
    m = re.search(r"ID_SERIAL_SHORT=([0-9A-F]{12})\b", usb)
    return m.group(1) if m else ""


def board_pin():
    """the board's pairing PIN as the app gets it (GET /v1/devices/<id>/pin), for BleOp's raw sessions. Never logged"""
    dev = board_id()
    if not dev:
        return ""
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}/pin", timeout=5) as f:
        return json.loads(f.read())["pin"]


def adb(*a, secs=180):
    return subprocess.run(["adb", *a], capture_output=True, text=True, timeout=secs).stdout


def awake():
    """the screen on and the lock screen gone: on battery `svc power stayon` does not hold, the phone dozed after its
    timeout and Wi-Fi adb stopped answering (test040, 2026-11-04); the bench phone has a PIN lock (2026-11-05), typed
    from ~/.leakcam_phone_pin (600, not in git) as key events, never printed"""
    try:
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP", secs=10)
        adb("shell", "wm", "dismiss-keyguard", secs=10)
        if "mIsShowing=true" not in adb("shell", "dumpsys", "window", secs=15):
            return
        pin = (Path.home() / ".leakcam_phone_pin").read_text().strip()
        adb("shell", "input", "swipe", "540", "1800", "540", "600", "300", secs=10)
        time.sleep(1)
        adb("shell", "input", "keyevent", *[f"KEYCODE_{d}" for d in pin], "KEYCODE_ENTER", secs=15)
    except subprocess.TimeoutExpired:
        pass                                    # the op's own call then fails, logged


def keep_awake():
    """for the whole run: every 20 s the phone's wakefulness read; not Awake -> logged and woken (2026-11-04: the phone
    dozed mid-run and the test hung)"""
    from board import log

    def loop():
        while True:
            try:
                w = re.search(r"mWakefulness=(\w+)", adb("shell", "dumpsys", "power", secs=15))
                if not w or w.group(1) != "Awake":
                    log(f"phone: {w.group(1) if w else 'no answer'}: woken")
                    awake()
            except subprocess.TimeoutExpired:
                log("phone: no answer (wakefulness)")
            time.sleep(20)

    threading.Thread(target=loop, daemon=True).start()


def op(name, **kw):
    """one BleOp call; its JSON result (never printed whole: no values in it, but keep the log short). The arguments go
    in a file pushed to the phone and removed after the call: on the command line adbd logged them, the Wi-Fi
    passphrase in hex included, into the phone's logcat (2026-10-30)"""
    if name == "session" and "pin" not in kw:               # pin="": a phone without the app (no PIN)
        kw["pin"] = board_pin()
    from board import log
    # bounded by the op's own length: a slow or stuck phone (Wi-Fi adb, 2026-11-04: test040 silent for 22 min) is a
    # logged error within a minute, never a silent wait
    limit = int(kw.get("secs", 10)) + 60
    t0 = time.time()
    awake()
    fd, local = tempfile.mkstemp(suffix=".json")          # mode 600
    try:
        with os.fdopen(fd, "w") as f:
            json.dump({"op": name, **{k: str(v) for k, v in kw.items()}}, f)
        adb("push", local, ARGS_FILE, secs=30)
        adb("shell", "chmod", "644", ARGS_FILE, secs=30)    # the test app reads it (another uid)
        out = adb("shell", "am", "instrument", "-w", "-r", "-e", "argsfile", ARGS_FILE, "-e", "op", name, RUNNER,
                  secs=limit)
    except subprocess.TimeoutExpired as e:
        out = ""
        log(f"phone: {name} no answer in {e.timeout:.0f} s")
    finally:
        os.unlink(local)
        try:
            adb("shell", "rm", "-f", ARGS_FILE, secs=30)
        except subprocess.TimeoutExpired:
            log("phone: args file not removed (no answer)")
    m = re.search(r"INSTRUMENTATION_STATUS: result=(.*)", out)
    res = json.loads(m.group(1)) if m else {"error": "no result from the phone"}
    log(f"phone: {name} {time.time() - t0:.1f} s" + (f", error {res['error']}" if res.get("error") else ""))
    return res


def product():
    """True on a BL616 product firmware (firmware/bl616_full or bl616_setup: `provision`), False on the test firmware
    (bl616_powertest: `pt_ble`)"""
    # its own answer, not the help listing: since presence (test111) the Wi-Fi logs interleave and cut `help` short
    return any("provision: pairing PIN" in bl("provision", 2, r"provision: pairing PIN") for _ in range(2))


def firmware():
    """'full', 'setup' or the image `slot` names (setup_test, powertest), None when it does not answer; asked again
    when the full firmware's Wi-Fi logs drown the answer (test126 which())"""
    for _ in range(5):
        try:
            m = re.search(r"leakcam-build (\S+)", bl("slot", 3, r"leakcam-build \S+"))
        except OSError:                         # the port went with a slot switch mid-write (EIO, test119 2026-11-03)
            time.sleep(2)
            continue
        if m:
            return m.group(1)
    return None


def to_firmware(want, cmd=None, secs=60):
    """seconds until the want firmware answers (switched by cmd when another one runs, or by the board itself), None
    if not in secs (setup -> full: the phone leaves in <= 15 s, then the slot switch; test106)"""
    t0 = time.time()
    while time.time() - t0 < secs:
        fw = firmware()                         # bl() waits up to 30 s for a port gone in a switch
        if fw == want:
            return time.time() - t0
        if fw and cmd:
            bl(cmd, 1)
            cmd = None
        time.sleep(2)
    return None


HAND_FILE = os.path.expanduser("~/.leakcam_hand")


def hand(action, word, secs=900):
    """a hand step (USB-C 1 out / in): logged as ACTION, done once HAND_FILE holds word (`echo <word> > ~/.leakcam_hand`,
    written by whoever watches the bench); the time it was done, None if not in secs"""
    from board import log
    try:
        os.remove(HAND_FILE)
    except FileNotFoundError:
        pass
    log(f"ACTION: {action} (then: echo {word} > {HAND_FILE})")
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            if open(HAND_FILE).read().strip() == word:
                return time.time()
        except FileNotFoundError:
            pass
        time.sleep(0.5)
    return None


def seen(dev, secs=60):
    """seconds until the phone's scan finds dev, None if not in secs: on battery the setup firmware sleeps with its
    console off (bugs/bug-006 runs), so the radio is the only sign of it"""
    t0 = time.time()
    while time.time() - t0 < secs:
        res = op("scan", secs=20)
        if res.get("found") and res.get("name") == dev:
            return time.time() - t0
    return None


_phone = []


def phone_id():
    """the app's phone id (its Android ID, one per app signing key: not `settings get secure android_id`), as the app
    logs it at its start ("phone <id>"); the server knows the phone's users by it (DESIGN.md 5.3.5)"""
    if not _phone:
        adb("logcat", "-c")
        adb("shell", "am", "start", "-n", "com.leakcam.setup/.MainActivity")
        t0 = time.time()
        while time.time() - t0 < 15 and not _phone:
            m = re.search(r"phone ([0-9a-f]{16})", adb("logcat", "-d", "-s", "LeakcamSetup:I", secs=30))
            if m:
                _phone.append(m.group(1))
            time.sleep(0.5)
        adb("shell", "am", "force-stop", "com.leakcam.setup")
    if not _phone:
        raise RuntimeError("the app did not log its phone id")
    return _phone[0]


def phone_boards():
    """the boards the server lists for this phone (GET /v1/phones/<phone>/devices): the app's list"""
    with urllib.request.urlopen(f"{SERVER}/v1/phones/{phone_id()}/devices", timeout=5) as f:
        return [d["id"] for d in json.loads(f.read())]


def register(dev):
    """this phone a user of dev at the server, as a setup does (the server registers it when it issues the PIN; the PIN
    itself is dropped, never logged)"""
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}/pin?phone={phone_id()}", timeout=5) as f:
        f.read()


def unregister(dev):
    """this phone no longer a user of dev (the app's delete); True when the server confirmed"""
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/phones/{phone_id()}", method="DELETE")
    try:
        with urllib.request.urlopen(req, timeout=5) as f:
            return f.status == 200
    except urllib.error.HTTPError as e:
        return e.code == 404


def only_boards(*devs):
    """the app's list (the server's, for this phone) holds just these: the run's starting state"""
    for d in phone_boards():
        if d not in devs:
            unregister(d)
    for d in devs:
        register(d)


def online(dev):
    """the presence server's view of dev"""
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
            return json.loads(f.read()).get("online", False)
    except OSError:
        return False


def creds():
    """(ssid len, crc), (psk len, crc) as stored on the BL616 (`provision` / `pt_ble creds`, never the passphrase); None
    for a missing key"""
    out = bl("provision" if product() else "pt_ble creds", 2, r"wifi_psk[^\n]*\n")
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
    if "device" not in adb("get-state", secs=10) and os.path.exists(PHONE_FILE):
        ip, _, port = open(PHONE_FILE).read().strip().rpartition(":")
        for host in dict.fromkeys((ip, *PHONE_IPS)):
            if "connected to" in adb("connect", f"{host}:{port}", secs=10):
                open(PHONE_FILE, "w").write(f"{host}:{port}\n")
                break
    if "device" not in adb("get-state", secs=10):
        return f"no Android phone on adb: Wireless debugging on, its <ip>:<port> in {PHONE_FILE} (the port changes when it is switched off and on)"
    for pkg, apk in (("com.leakcam.setup", APK), ("com.leakcam.setup.test", TEST_APK)):
        # only a changed APK is installed: every install over USB brings up Play Protect's "verify this app" (2026-10-28)
        on_phone = adb("shell", "pm", "path", pkg).strip().replace("package:", "")
        if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(apk, "rb").read()).hexdigest()]:
            install(apk, notes)
    adb("shell", "svc", "power", "stayon", "true")  # the screen stays on (no cable): the pairing dialog shows
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "cmd", "bluetooth_manager", "enable")
    # a locked phone shows the pairing dialog over the lock screen, where UI Automator cannot press it (2026-10-28)
    awake()
    if "mIsShowing=true" in adb("shell", "dumpsys", "window", secs=15):   # Samsung: mIsShowing, no isKeyguardShowing
        return "the phone is locked and the PIN in ~/.leakcam_phone_pin did not open it"
    keep_awake()
    return None
