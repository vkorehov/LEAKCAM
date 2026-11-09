"""The Android bench phone for the BLE tests (test040, test106): adb over Wi-Fi, the instrumented BleOp
(android/app/src/androidTest), installs that answer Play Protect, and the phone made ready for a run (skill
android-bench-phone)."""
import base64
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


def server_up(secs=15):
    """seconds until the server answers again (after `systemctl start`), None if not in secs; in place of a fixed
    sleep (user 2026-11-09: "no need to wait")"""
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            with urllib.request.urlopen(SERVER + "/", timeout=2):
                return time.time() - t0
        except OSError:
            time.sleep(0.1)
    return None


def clear_leak(dev):
    """the board's open leak cleared at the server (POST /v1/devices/<id>/clear, the all-clear): an open leak holds
    the board in the 1-min wake mode, which overrides every interval a test sets (test108's own leak check opened one
    and test134, test108 failed after it, 2026-11-09)"""
    if dev:
        urllib.request.urlopen(urllib.request.Request(f"{SERVER}/v1/devices/{dev}/clear", method="POST"), timeout=10)


def intervals(dev, checkin=0, leakcheck=0):
    """the board's check-in and leak check intervals at the server (PUT /v1/devices/<id>/intervals, DESIGN.md 5.3.12),
    seconds, 0 = the server's default (the bench's LEAKCAM_CHECKIN_S 60, leak check 86400); the board takes them with
    its next check-in. The server's answer: the intervals now"""
    if not dev:
        raise ValueError("intervals: no device ID (the board's port gone?)")
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/intervals", method="PUT",
                                 data=json.dumps({"checkin": checkin, "leakcheck": leakcheck}).encode())
    with urllib.request.urlopen(req, timeout=10) as f:
        ans = json.loads(f.read())
    if dev == board_id():
        bl("presence now", 1)                # the bench board takes them at once, not after its old interval
    return ans


def board_checkin():
    """the board's check-in interval as it took it from the server's last answer (`presence`), 0 = no answer"""
    m = re.search(r"check-in every (\d+) s", bl("presence", 2, r"check-in every \d+ s"))
    return int(m.group(1)) if m else 0


_board_id = ""


def board_id():
    """the board's serial (device ID) from its USB serial number; the last one seen while the console is away (a
    firmware switch: it never changes), "" when never seen"""
    global _board_id
    from board import BL_PORT
    usb = subprocess.run(["udevadm", "info", "-q", "property", BL_PORT], capture_output=True, text=True).stdout
    m = re.search(r"ID_SERIAL_SHORT=([0-9A-F]{12})\b", usb)
    if m:
        _board_id = m.group(1)
    return _board_id


def ack_alerts():
    """every leak alert on the phone acknowledged at once (the debug build's TEST_ACK): a test alert never rings longer
    than its checks need (user, 2026-11-07: "acknowledge yourself faster, it is annoying")"""
    # -f 32: also when the app was force-stopped (Android drops broadcasts to stopped apps otherwise)
    adb("shell", "am", "broadcast", "-f", "32", "-a", "com.leakcam.setup.TEST_ACK", "-p", "com.leakcam.setup", secs=15)


def board_pin():
    """the board's pairing PIN as the app gets it (GET /v1/devices/<id>/pin), for BleOp's raw sessions. Never logged"""
    dev = board_id()
    if not dev:
        return ""
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}/pin", timeout=5) as f:
        return json.loads(f.read())["pin"]


def adb(*a, secs=180):
    return subprocess.run(["adb", *a], capture_output=True, text=True, timeout=secs).stdout


_dim = []
_awake = [0.0]
AWAKE_S = 30


def awake():
    """the screen on and the lock screen gone: on battery `svc power stayon` does not hold, the phone dozed after its
    timeout and Wi-Fi adb stopped answering (test040, 2026-11-04); the bench phone has a PIN lock (2026-11-05), typed
    from ~/.leakcam_phone_pin (600, not in git) as key events, never printed; True once unlocked (the runner unlocks it,
    never the user: 2026-11-07)"""
    def locked():
        return "mIsShowing=true" in adb("shell", "dumpsys", "window", secs=15)   # Samsung: no isKeyguardShowing

    def unlocked():
        # the notification shade left open hides the app from UI Automator: "no setup screen; on screen: " with the
        # focus on NotificationShade (test129 --battery, 2026-11-08)
        adb("shell", "cmd", "statusbar", "collapse", secs=10)
        if not _dim:                   # the screen at minimum brightness while testing: less power (user 2026-11-08)
            adb("shell", "settings", "put", "system", "screen_brightness_mode", "0", secs=10)
            adb("shell", "settings", "put", "system", "screen_brightness", "1", secs=10)
            _dim.append(1)
        _awake[0] = time.time()
        return True

    # unlocked less than AWAKE_S ago: the screen stays on (stay_on_while_plugged_in / its timeout is longer): the four
    # adb calls cost ~1.5 s per op, a pause between every relay step (user 2026-11-08: "again very long pauses")
    if time.time() - _awake[0] < AWAKE_S:
        return True
    try:
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP", secs=10)
        adb("shell", "wm", "dismiss-keyguard", secs=10)
        pin = (Path.home() / ".leakcam_phone_pin").read_text().strip()
        for _ in range(3):
            if not locked():
                return unlocked()
            adb("shell", "input", "swipe", "540", "1800", "540", "600", "300", secs=10)
            time.sleep(0.3)
            adb("shell", "input", "keyevent", *[f"KEYCODE_{d}" for d in pin], "KEYCODE_ENTER", secs=15)
            for _ in range(25):                 # right after the PIN it still reads locked for a moment (2026-11-07)
                time.sleep(0.2)
                if not locked():
                    return unlocked()
        return False
    except subprocess.TimeoutExpired:
        return False                            # the op's own call then fails, logged


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


SCREEN_RUNNER = "com.leakcam.setup.test/com.leakcam.setup.SelfRunner"


def screen_xml():
    """the screen's view tree (UI Automator XML) from the test APK's Screen dump: it waits at most 300 ms for an idle
    screen, `uiautomator dump` up to 10 s (the setup screen's spinner never idles: 10 s per dump, test135 2026-11-08),
    and it leaves the app running; `uiautomator dump` if that fails"""
    out = adb("shell", "am", "instrument", "-w", "-r", "-e", "class", "com.leakcam.setup.Screen", SCREEN_RUNNER, secs=30)
    m = re.search(r"INSTRUMENTATION_STATUS: xml=(\S+)", out)
    if m:
        return base64.b64decode(m.group(1)).decode("utf-8", "replace")
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml", secs=30)
    return adb("shell", "cat", "/sdcard/ui.xml", secs=30)


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
            time.sleep(0.5)
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
        time.sleep(0.3)
    return None


HAND_FILE = os.path.expanduser("~/.leakcam_hand")


RELAY = "A4:06:E9:9D:30:7E"     # the bench relay box: HM-10 LCRELAY (skill leakcam-bench-relays)
# PIO LOW = relay on = phone side. K1 PIO2 D+ (green), K2 PIO3 D- (white), K3 PIO4 CC (yellow), K4 PIO5 VBUS (red).
# Unplug = K4 + K1 + K2 at once (VBUS, D+, D- onto the phone side, where nothing answers while CC stays on the bench):
# the bench port gives 5 V always (a CC-only unplug left the board powered), and with D+/D- still on the PC a
# VBUS-only unplug kept the board a live USB device on its cell - console up, the setup firmware never took its
# unplug path (test116 2026-11-08: no adverts in 60 s). HMSoft sets the pins back to its power-on state (BEFC) when
# the phone disconnects, so every step first stores its end state as BEFC.
RELAY_STEPS = {                 # phases of relays to switch on (K1 D+, K2 D-, K3 CC, K4 VBUS); 1 s between phases
    "unplugged": [{1, 2, 4}], "battery": [{1, 2, 4}],
    "plugged": [set()], "usb": [set()],
    "phone": [{1, 2, 4}, {1, 2, 3, 4}],     # unplug, then CC to the phone: it sees the board, powers and enumerates it
    "bench": [{1, 2, 4}, set()],            # CC off the phone first (its 5 V stops), then every relay off = the bench
}


def bits(on):
    """HMSoft's PIO2..PIOB word (10 bits, leftmost PIO2; 1 = HIGH = relay off) with relays on (K1..K4 = PIO2..5)"""
    return "%03X" % sum(0x200 >> (k - 1) for k in (1, 2, 3, 4) if k not in on)


relay_left = [None]                     # the relays left on by the last relay() of this process (None: not used)


def plug_back():
    """USB-C 1 back on the bench if this process left it anything else: a test stopped between its unplug and its
    plug left the board half-plugged (VBUS off, the data lines back) and the next test's setup firmware slept on USB
    (2026-11-09, test129 --battery -> test116); Run.finish() calls it"""
    if relay_left[0]:
        relay([set()])


RELAY_APP = "com.relay"
RELAY_APK = os.path.join(REPO, "tests/firmware/bench_relays/relay-app/app/build/outputs/apk/debug/app-debug.apk")
_relay_n = [0]


def relay(phases, gaps=None, times=None):
    """the relay box over BLE through the bench phone's own relay app (com.relay, tests/firmware/bench_relays/relay-app):
    it keeps one connection open, so a switch is ~0.5 s from here (~4 s with a connect each), and it never touches the
    LEAKCAM app (the old way, an instrumentation of the app, force-stopped the app at every switch, 2026-11-09).
    BEFC = the end state (kept after a disconnect), then one AT+MPIO per phase - every relay of a phase switches at
    once (separate AT+PIOnv writes switched them ~0.5 s apart, 2026-11-08) - gaps[i] ms after phase i's reply (default
    1000 each; each command also waits for its reply, ~one BLE connection interval); times: a list that gets each
    phase's reply time (phone ms); True when every command was answered as asked"""
    from board import log
    gaps = gaps or [1000] * (len(phases) - 1)
    cmds = ["AT+BEFC" + bits(phases[-1])]
    for i, on in enumerate(phases):
        cmds += ([f"wait:{gaps[i - 1]}"] if i else []) + ["AT+MPIO" + bits(on)]
    want = ["OK+Set:" + c[-3:] for c in cmds if c.startswith("AT+")]
    relay_left[0] = phases[-1]              # what the box holds after this op (BEFC), for the end of the test
    if _relay_n[0] == 0 and os.path.exists(RELAY_APK):    # the relay app installed / current, once per process
        on_phone = adb("shell", "pm", "path", RELAY_APP).strip().replace("package:", "")
        if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(RELAY_APK, "rb").read()).hexdigest()]:
            install(RELAY_APK, [])
    _relay_n[0] += 1
    rid = f"{os.getpid()}-{_relay_n[0]}"
    t0 = time.time()
    adb("shell", "am", "start-foreground-service", "-n", f"{RELAY_APP}/.RelayService", "--es", "id", rid,
        "--es", "address", RELAY, "--es", "cmds", ",".join(cmds), secs=15)
    res, limit = None, 20 + sum(gaps) / 1000
    while time.time() - t0 < limit:
        m = re.search(rf"RESULT {re.escape(rid)} (\{{.*\}})", adb("logcat", "-d", "-s", "LeakcamRelay:I", secs=15))
        if m:
            res = json.loads(m.group(1))
            break
        time.sleep(0.05)
    res = res or {"error": "no answer from the relay app"}
    log(f"phone: relay {time.time() - t0:.1f} s" + (f", error {res['error']}" if res.get("error") else ""))
    replies = res.get("replies", [])
    if times is not None:
        times += [r.get("ms", 0) for r in replies if r.get("cmd", "").startswith("AT+MPIO")]
    return [r.get("reply", "") for r in replies] == want

def hand(action, word, secs=900):
    """a hand step (USB-C 1 out / in, bench <-> phone): done by the relay box when it knows the word (RELAY_STEPS),
    else (or if the box does not answer) logged as ACTION and done once HAND_FILE holds word (`echo <word> >
    ~/.leakcam_hand`, written by whoever watches the bench); the time it was done, None if not in secs"""
    from board import log
    if word in RELAY_STEPS:
        if relay(RELAY_STEPS[word]):
            log(f"relay box: {word} ({action})")
            return time.time()
        log(f"relay box: no answer for {word}, falling back to hands")
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
        awake()                                 # locked after a plug: the app never started (test113, 2026-11-08)
        # running already (a board's plug launched it): am start only brings it to the front, no "phone" line
        adb("shell", "am", "force-stop", "com.leakcam.setup")
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
    for _ in range(3):                          # a first command after the port opened can go unanswered (test136)
        out = bl("provision" if product() else "pt_ble creds", 2, r"wifi_psk[^\n]*\n")
        if "wifi_psk" in out:
            break
    got = []
    for key in ("wifi_ssid", "wifi_psk"):
        m = re.search(key + r" len (\d+) crc ([0-9a-f]{8})", out)
        got.append((int(m.group(1)), int(m.group(2), 16)) if m else None)
    return tuple(got)


def erase(tries=3):
    """`provision erase`, checked by its answer: once (test113 2026-11-07, the full firmware just back from a cable
    setup) the command never ran and the board kept its network; each retry is logged. True when erased"""
    from board import log
    for i in range(tries):
        if "credentials and remembered AP erased" in bl("provision erase", 2, r"erased|erase failed"):
            # the full firmware sees the erase at its next check-in (up to 60 s of an idle bench, user 2026-11-09:
            # "why phone is idle?"): one asked at once; the setup firmware has no `presence` (not found, at once)
            bl("presence now", 1, r"check-in now|not found")
            return True
        log(f"board: provision erase not answered (try {i + 1} of {tries})")
    return False


def want(ssid, psk):
    return tuple((len(v.encode()), zlib.crc32(v.encode())) if v is not None else None for v in (ssid, psk))


GRACE_S = 10                                    # Play Protect's dialog after an install over Wi-Fi adb: up to this late


def install(apk, notes):
    """adb install, answering Play Protect's "verify this app" dialog on the phone (an install over USB waits for it):
    its "install without checking" button is tapped from a UI dump (2026-10-28, user: no hand on the phone); the test
    APK's dialog says "Не проверять" (2026-10-30). Over Wi-Fi adb the install returns first and the dialog ("Проверить
    безопасность приложения?") comes after it: watched GRACE_S more (2026-11-08)"""
    done = threading.Event()
    answered = threading.Event()

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
                answered.set()

    t = threading.Thread(target=press, daemon=True)
    t.start()
    adb("install", "-r", "-g", "-t", apk)       # -g: the Bluetooth permissions granted at install
    answered.wait(GRACE_S)
    done.set()
    t.join()


def usb_seen(plugged, secs=5):
    """seconds until the board's evlog shows USB-C 1 plugged / unplugged as its latest USB event (the relay switched:
    the firmware's own edge, in place of a fixed sleep; user 2026-11-09), None if not in secs or no console"""
    t0 = time.time()
    want = "plugged" if plugged else "unplugged"
    while time.time() - t0 < secs:
        log = re.sub(r"\x1b\[[0-9;]*m", "", bl("evlog", 2, r"evlog: (?:empty|\d+ events[\s\S]*?/>)|not found"))
        usb = re.findall(r" usb +(plugged|unplugged)", log)
        if usb and usb[-1] == want:
            return time.time() - t0
        time.sleep(0.1)
    return None


def wedge():
    """None, or why the board's BLE core is wedged (skill bl616-ble-low-power 5): a WEDGE in the retained evlog (the
    setup firmware's detector: no advertising wake in a sleep window, or a BLE start stuck), or the live core of the
    setup firmware dead (`blecore`: 0x28000100 reads 0 or keeps bit 31, healthy 0000f255-like). Only the EN pin or a
    power loss clears it, so a test on a wedged board fails for nothing (2026-11-08: test116 "not found" for hours)"""
    from board import BL_PORT
    if not os.path.exists(BL_PORT):             # on the cell: no console; the next plugged start checks it
        return None
    clean = lambda t: re.sub(r"\x1b\[[0-9;]*m", "", t)
    log = clean(bl("evlog", 3, r"evlog: (?:empty|\d+ events[\s\S]*?/>)|not found"))
    hits = [l.strip() for l in log.splitlines() if " WEDGE " in l]
    if hits:
        return ("BLE core wedged (evlog: " + hits[-1] + "): power-cycle the board (cell out and USB-C off ~10 s, which "
                "also clears the evlog), or tap EN and then `evlog clear`")
    # right after a wake a healthy core reads 0 too (test139, 2026-11-09): dead = 0 or bit 31 in every read for 2 s,
    # polled 0.1 s apart; a live core answers at the first read (no fixed wait, user 2026-11-09)
    t0, vals = time.time(), []
    while True:
        m = re.search(r"BLE_0x100 +([0-9a-f]{8})", clean(bl("blecore", 3, r"BLE_0x100 +[0-9a-f]{8}|not found")))
        if not m:
            return None                         # the full firmware (no BLE) or the test firmware: no live check
        v = int(m.group(1), 16)
        if v and not v >> 31:
            return None
        vals.append(v)
        if time.time() - t0 > 2:
            break
        time.sleep(0.1)
    return f"BLE core wedged (0x28000100 read {len(vals)} times in 2 s: {', '.join('%08x' % v for v in vals[-3:])}): " \
           "power-cycle the board, or tap EN"


def ready(notes):
    """the phone made ready for a run, and the board's BLE core not wedged (wedge()); None, or why it cannot run"""
    why = wedge()
    if why:
        return why
    if "device" not in adb("get-state", secs=10) and os.path.exists(PHONE_FILE):
        ip, _, port = open(PHONE_FILE).read().strip().rpartition(":")
        for host in dict.fromkeys((ip, *PHONE_IPS)):
            try:
                answer = adb("connect", f"{host}:{port}", secs=10)
            except subprocess.TimeoutExpired:   # a host that does not answer: the next IP (it crashed here, 2026-11-06)
                continue
            if "connected to" in answer:
                open(PHONE_FILE, "w").write(f"{host}:{port}\n")
                break
    if "device" not in adb("get-state", secs=10):
        return f"no Android phone on adb: Wireless debugging on, its <ip>:<port> in {PHONE_FILE} (the port changes when it is switched off and on)"
    # portrait: in landscape the dialogs' buttons (Play Protect's "Не проверять") sit below the screen and are not in the
    # UI dump, and the unlock swipe is portrait's (2026-11-08)
    adb("shell", "settings", "put", "system", "accelerometer_rotation", "0")
    adb("shell", "settings", "put", "system", "user_rotation", "0")
    for pkg, apk in (("com.leakcam.setup", APK), ("com.leakcam.setup.test", TEST_APK)):
        # only a changed APK is installed: every install over USB brings up Play Protect's "verify this app" (2026-10-28)
        on_phone = adb("shell", "pm", "path", pkg).strip().replace("package:", "")
        if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(apk, "rb").read()).hexdigest()]:
            install(apk, notes)
    adb("shell", "svc", "power", "stayon", "true")  # the screen stays on (no cable): the pairing dialog shows
    adb("shell", "settings", "put", "global", "leakcam_fast_timing", "1")   # the app's periods 3x faster (debug build)
    # a test alert posts without sound or vibration (debug build; user 2026-11-07: "i want quiet night"); test133
    # turns it off for its sound checks only when asked (--loud)
    adb("shell", "settings", "put", "global", "leakcam_quiet", "1")
    # the leak alert's permissions granted here: the app asks for them after a Done, and a settings screen would stop
    # the run (DESIGN.md 5.3.13)
    adb("shell", "pm", "grant", "com.leakcam.setup", "android.permission.POST_NOTIFICATIONS")
    adb("shell", "appops", "set", "com.leakcam.setup", "USE_FULL_SCREEN_INTENT", "allow")
    adb("shell", "cmd", "notification", "allow_dnd", "com.leakcam.setup")
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "cmd", "bluetooth_manager", "enable")
    # a locked phone shows the pairing dialog over the lock screen, where UI Automator cannot press it (2026-10-28)
    if not awake():
        return "the phone is locked and the PIN in ~/.leakcam_phone_pin did not open it"
    keep_awake()
    return None
