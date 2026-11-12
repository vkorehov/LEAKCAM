"""The Android bench phone for the BLE tests (test040, test106): adb over Wi-Fi, the instrumented BleOp
(android/app/src/androidTest), installs that answer Play Protect, and the phone made ready for a run (skill
android-bench-phone)."""
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
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
SERVER = "http://192.168.1.131:8090"   # the presence server (server/, the Windows PC since 2026-11-12, server/windows): the board's pairing PIN comes from it


BOARD_SERVER = ("192.168.1.131", 8443)  # the boards' port: mTLS only (server/mtls.go, DESIGN.md 5.3.12)
PKI = os.path.join(REPO, ".secrets", "pki")


def as_board(dev, path, form=b"", method="POST"):
    """a request as the board `dev` makes it: over mTLS with that ID's own certificate (tools/pki/leakcam_pki.py
    issues one for a test ID once), the server checked against the LEAKCAM CA; (status, body bytes)"""
    import http.client
    import ssl
    crt, key = (os.path.join(PKI, "devices", f"{dev}{s}") for s in (".crt", ".key"))
    if not os.path.exists(crt):
        subprocess.run([sys.executable, os.path.join(REPO, "tools", "pki", "leakcam_pki.py"), "device", dev],
                       check=True, capture_output=True)
    ctx = ssl.create_default_context(cafile=os.path.join(PKI, "ca.crt"))
    ctx.load_cert_chain(crt, key)
    c = http.client.HTTPSConnection(*BOARD_SERVER, context=ctx, timeout=10)
    try:
        c.request(method, path, body=form if method == "POST" else None,
                  headers={"Content-Type": "application/x-www-form-urlencoded"} if method == "POST" else {})
        r = c.getresponse()
        return r.status, r.read()
    finally:
        c.close()


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


def phone_clean(dev=None):
    """the bench left as found, after every test and every bench script (user 2026-11-12, twice: "again on phone
    unacknowledged notification"): the board's leak cleared at the server (a `changed` check or a wet probe opens one,
    and its push rings), every alert acked, then no LEAKCAM notification may be left (a `media` push from an upload is
    one too); one left after the ack: the app force-stopped, which drops them (11-12). Returns how many were left"""
    try:
        clear_leak(dev or board_id())
    except Exception as e:                     # noqa: BLE001 - the phone part still runs
        print(f"phone_clean: leak not cleared: {e!r}", flush=True)
    if "device" not in adb("get-state", secs=5):
        return 0
    ack_alerts()
    left = 0
    for _ in range(10):
        left = adb("shell", "dumpsys", "notification", "--noredact", secs=15).count("pkg=com.leakcam.setup")
        if not left:
            return 0
        time.sleep(0.5)
    adb("shell", "am", "force-stop", "com.leakcam.setup", secs=15)
    print(f"phone_clean: {left} LEAKCAM notifications left after the ack: the app force-stopped", flush=True)
    return left


def board_pin():
    """the board's pairing PIN as the app gets it (GET /v1/devices/<id>/pin), for BleOp's raw sessions. Never logged"""
    dev = board_id()
    if not dev:
        return ""
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}/pin", timeout=5) as f:
        return json.loads(f.read())["pin"]


_reconnecting = []


def adb(*a, secs=180):
    """one adb call; a short one (<= 30 s) that times out with the phone gone from adb (Wi-Fi rejoined at its other
    address, a new port: test140/141 2026-11-11) is asked again once after adb_up() found it"""
    try:
        return subprocess.run(["adb", *a], capture_output=True, text=True, timeout=secs).stdout
    except subprocess.TimeoutExpired:
        if secs > 30 or _reconnecting:
            raise
        _reconnecting.append(1)
        try:
            from board import log
            log(f"phone: adb {a[0]} timed out: reconnecting")
            if not adb_up():
                raise
        finally:
            _reconnecting.clear()
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
    res = _op_once(name, kw, limit)
    # Wi-Fi adb gone under the call (test117 --battery 2026-11-11: ~20 s without adb, the op and every scan after it
    # failed at once): adb back (adb_up: the recorded port, else the relay app's) and the op once more
    if res.get("error") == "no result from the phone" and "device" not in adb("get-state", secs=10):
        log(f"phone: adb lost during {name}: reconnecting, {name} again")
        if adb_up():
            res = _op_once(name, kw, limit)
    log(f"phone: {name} {time.time() - t0:.1f} s" + (f", error {res['error']}" if res.get("error") else ""))
    return res


def _op_once(name, kw, limit):
    from board import log
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
    return json.loads(m.group(1)) if m else {"error": "no result from the phone"}


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
    t0, sent, at = time.time(), 0, 0.0
    while time.time() - t0 < secs:
        fw = firmware()                         # bl() waits up to 30 s for a port gone in a switch
        if fw == want:
            took = time.time() - t0
            # a full firmware started on the cell (a setup on battery) sleeps in HBN once its first check-in is done,
            # and its USB console goes with it: the next console step found no port (test116 2026-11-10, step 4).
            # Kept awake as hand("battery") does; test141 tests the sleep itself (KEEP_AWAKE False)
            if want == "full" and KEEP_AWAKE:
                bl("hbn off", 1)
            return took
        # sent again while the old firmware still answers 5 s later: a command sent once was lost now and then and the
        # wait sat its whole bound (test136 2026-11-09: "back" in 0 s, no switch at all); never during the switch
        # (the port gone: firmware() None)
        if fw and cmd and sent < 3 and time.time() - at > 5:
            bl(cmd, 1)
            sent, at = sent + 1, time.time()
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
# ms between the phases where 1 s is not enough: VBUS off 1 s before the move to the phone was missed by the board
# now and then (no unplug seen, the K230 never enumerated on the phone: test113 2026-11-09, 1 of 3); 2 s: 6 of 6
RELAY_GAPS = {"phone": [2000]}
KEEP_AWAKE = True               # hand("battery"): `hbn off` first (the console on battery); test141 tests the sleep itself


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
    # the relay app installed / current, once per process - only with adb up (session_start() switches the relays over
    # HTTP precisely when adb is not)
    if _relay_n[0] == 0 and os.path.exists(RELAY_APK) and "device" in adb("get-state", secs=10):
        on_phone = adb("shell", "pm", "path", RELAY_APP).strip().replace("package:", "")
        if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(RELAY_APK, "rb").read()).hexdigest()]:
            install(RELAY_APK, [])
    for attempt in (1, 2):    # a broken link ("write failed", 2026-11-09: the hand fallback then sat 15 min) is
        res = _relay_once(cmds, gaps)     # dropped by the app: the second request connects anew; the end state is
        if not res.get("error"):          # absolute (BEFC, MPIO), so the whole sequence again is safe
            break
    replies = res.get("replies", [])
    if times is not None:
        times += [r.get("ms", 0) for r in replies if r.get("cmd", "").startswith("AT+MPIO")]
    return [r.get("reply", "") for r in replies] == want


RELAY_PORT = 8765                                   # the relay app's remote port (RelayService.listen)
RELAY_TOKEN = os.path.expanduser("~/.leakcam_relay_token")   # `adb shell run-as com.relay cat /data/user_de/0/com.relay/files/token`, 600


def _relay_http(cmds, gaps):
    """the request over the relay app's HTTP port on the phone's LAN address (no adb: user 2026-11-10, "add remote
    possibility to relay app"); its result (dict), None when the port or the token is not there (adb then)"""
    if not os.path.exists(RELAY_TOKEN) or not os.path.exists(PHONE_FILE):
        return None
    ip = open(PHONE_FILE).read().strip().rpartition(":")[0]
    req = urllib.request.Request(f"http://{ip}:{RELAY_PORT}/run?address={RELAY}", data=",".join(cmds).encode(),
                                 headers={"X-Token": open(RELAY_TOKEN).read().strip()}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=20 + sum(gaps) / 1000) as f:
            return json.loads(f.read())
    except (OSError, ValueError):
        return None


def _relay_once(cmds, gaps):
    """one request to the relay app, over its HTTP port first, adb's intent + logcat when that is not reachable: its
    result (dict)"""
    from board import log
    t0 = time.time()
    res = _relay_http(cmds, gaps)
    if res is not None:
        log(f"phone: relay {time.time() - t0:.1f} s (http)" + (f", error {res['error']}" if res.get("error") else ""))
        return res
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
    return res

def hand(action, word, secs=900):
    """a hand step (USB-C 1 out / in, bench <-> phone): done by the relay box when it knows the word (RELAY_STEPS; None
    at once if the box does not answer), else (a real hand step: test132's wet probe) logged as ACTION and done once HAND_FILE holds word (`echo <word> >
    ~/.leakcam_hand`, written by whoever watches the bench); the time it was done, None if not in secs"""
    from board import log
    if word in ("battery", "unplugged") and KEEP_AWAKE and firmware() == "full":
        # the full firmware sleeps in HBN on battery (DESIGN.md 5.3.12): its console gone between wakes; a test that
        # talks to it on battery keeps it awake until the next reset (`hbn off`); test141 sets KEEP_AWAKE False
        from harness import bl
        bl("hbn off", 1)
    if word in RELAY_STEPS:
        if relay(RELAY_STEPS[word], RELAY_GAPS.get(word)):
            log(f"relay box: {word} ({action})")
            return time.time()
        log(f"relay box: no answer for {word}: the step failed (no hand fallback: nobody watches a hands-free run, "
            "a fallback sat 15 min, 2026-11-09)")
        return None
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
    # the setup test bed (build "setup_test") starts BLE only with `lp_boot`: before it the core is off and reads 0, which
    # is no wedge (2026-11-12: a fresh `make flash` of the bed stopped the batch as "wedged"); not started = no live check
    if re.search(r"leakcam-build setup_test", clean(bl("slot", 2, r"leakcam-build \S+|not found"))) and \
            "easyflash init failed" in clean(bl("pt_ble creds", 3, r"easyflash init failed|pairing PIN")):
        return None
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


def _wait(fn, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def connect():
    """adb to the bench phone, USB first (user, 2026-11-10: "always connect to phone using usb adb, and enable wifi
    debug and record port and IP"): with the phone on USB, the run uses that connection (ANDROID_SERIAL, for this
    process and the tests it starts); Wireless debugging enabled (its Wi-Fi is always on: user, 2026-11-10),
    and the current <ip>:<port> written to PHONE_FILE and connected too, so the cable can go at any time. Without a
    cable: the Wi-Fi connection from PHONE_FILE (ready()). The serial in use, or None"""
    usb = [l.split()[0] for l in adb("devices", secs=10).splitlines()[1:]
           if l.strip().endswith("device") and ":" not in l.split()[0]]
    if not usb:
        os.environ.pop("ANDROID_SERIAL", None)
        return None
    s = usb[0]
    sh = lambda *a, secs=15: adb("-s", s, "shell", *a, secs=secs)   # noqa: E731
    ip = _wait(lambda: (re.search(r"inet (\d+\.\d+\.\d+\.\d+)/", sh("ip", "-f", "inet", "addr", "show", "wlan0"))
                        or [None, None])[1], 30)
    sh("settings", "put", "global", "adb_wifi_enabled", "1")
    port = _wait(lambda: sh("getprop", "service.adb.tls.port").strip() or None, 15)
    if ip and port:
        open(PHONE_FILE, "w").write(f"{ip}:{port}\n")
        adb("connect", f"{ip}:{port}", secs=10)
    else:
        from board import log
        log(f"phone on USB: Wi-Fi {'up ' + ip if ip else 'no address in 30 s'}, Wireless debugging port "
            f"{port or 'not shown in 15 s'}: {PHONE_FILE} not updated")
    os.environ["ANDROID_SERIAL"] = s
    return s


def _only(fixed):
    """the phone's other adb entries (an old port, offline) dropped, and every adb call of this process and its tests
    on `fixed`: with two entries a plain `adb` refused ("more than one device", 2026-11-11)"""
    for line in adb("devices", secs=10).splitlines()[1:]:
        serial = line.split()[0] if line.strip() else ""
        if ":" in serial and serial != fixed:
            adb("disconnect", serial, secs=10)
    os.environ["ANDROID_SERIAL"] = fixed


def _relay_adb(ip):
    """(ip, Wireless debugging port) from the relay app's GET /adb, None when it does not answer"""
    if not os.path.exists(RELAY_TOKEN):
        return None
    req = urllib.request.Request(f"http://{ip}:{RELAY_PORT}/adb",
                                 headers={"X-Token": open(RELAY_TOKEN).read().strip()})
    try:
        with urllib.request.urlopen(req, timeout=20) as f:
            d = json.loads(f.read())
        return (d["ip"], d["port"]) if d.get("port", -1) > 0 else None
    except (OSError, ValueError, KeyError):
        return None


def adb_up():
    """adb to the phone over Wireless debugging: the recorded <ip>:<port> while it answers; else (a toggle, a phone
    restart: a new random port) the relay app's GET /adb switches Wireless debugging on and tells the current port
    (user, 2026-11-11: no fixed port 5555 any more) - connected (this PC is paired) and recorded. True when adb answers"""
    from board import log

    def state(serial):
        try:
            return "device" in adb("-s", serial, "get-state", secs=10)
        except subprocess.TimeoutExpired:
            return False

    def connect_to(serial):
        try:
            adb("connect", serial, secs=10)
        except subprocess.TimeoutExpired:           # a dead port: hung 10 s (2026-11-11), not an error
            pass
        return state(serial)

    if not os.path.exists(PHONE_FILE):
        return False
    ip, _, port = open(PHONE_FILE).read().strip().rpartition(":")
    serial = f"{ip}:{port}"
    if not state(serial) and not connect_to(serial):
        # one address: the phone joins only VK (VK_5GHZ removed, user 2026-11-11: "5GHZ is instable"; on it the phone
        # had moved to .163 and adb was lost)
        w = _relay_adb(ip)
        if w and connect_to(f"{w[0]}:{w[1]}"):
            adb("disconnect", serial, secs=10)
            serial = f"{w[0]}:{w[1]}"
            open(PHONE_FILE, "w").write(serial + "\n")
            log(f"phone: Wireless debugging moved to {serial} (from the relay app), recorded")
    up = state(serial)
    if up:
        _only(serial)
    return up


def session_start():
    """the start of a session: adb to the phone - over USB when it is on one (connect(): Wireless debugging on, the
    current <ip>:<port> recorded), else over Wi-Fi from PHONE_FILE. The bench has no USB path from the PC to the phone
    (2026-11-10: the relays only move the board's USB-C 1), so the relays are not switched for it; they work without
    adb anyway (the relay app's HTTP port). True when adb answers"""
    from board import log
    usb = connect()
    wifi = open(PHONE_FILE).read().strip() if os.path.exists(PHONE_FILE) else "?"
    if not usb and "device" not in adb("get-state", secs=10):
        adb("connect", wifi, secs=10)
    if not usb:
        adb_up()                                # a moved port: the relay app tells the new one
    wifi = open(PHONE_FILE).read().strip() if os.path.exists(PHONE_FILE) else "?"
    up = "device" in adb("get-state", secs=10)
    os.environ["LEAKCAM_PHONE_SESSION"] = "1"     # done for this process and the tests it starts (harness.Run)
    log(f"session start: phone on {'USB ' + usb if usb else 'Wi-Fi ' + wifi}, adb {'up' if up else 'DOWN'}"
        + ("" if up else f" (the relay app at {wifi.rpartition(':')[0]}:{RELAY_PORT} gave no live port: phone off "
                         "Wi-Fi, or its service not running)"))
    return up


def ready(notes):
    """the phone made ready for a run, and the board's BLE core not wedged (wedge()); None, or why it cannot run"""
    why = wedge()
    if why:
        return why
    connect()                                   # USB first, its Wi-Fi debugging recorded for the time without a cable
    # one address since 2026-11-11 (the phone joins only VK, 192.168.1.141); a moved port from the relay app
    if "device" not in adb("get-state", secs=10):
        adb_up()
    if "device" not in adb("get-state", secs=10):
        return f"no Android phone on adb: neither {PHONE_FILE} nor the relay app's /adb answered (phone off Wi-Fi?)"
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
