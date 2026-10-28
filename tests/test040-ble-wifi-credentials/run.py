#!/usr/bin/env python3
"""test040 automated: the BL616's BLE provisioning driven from an Android phone over adb (the instrumented BleOp of
android/, one BLE operation per call), the stored credentials checked on the BL616 console (`pt_ble creds`: length +
CRC-32, never the passphrase). Needs USB-C 1 in (pairing only on USB power), the phone on adb, unlocked, Bluetooth on.
The steps that need a hand on a cable (unplug, power cut) are not in here: README Steps 1, 2 and 5 say which.

    tests/test040-ble-wifi-credentials/run.py [--flash]    --flash: build output of bl616_powertest flashed first
"""
import fcntl
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time
import zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl, REPO   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402

APK = os.path.join(REPO, "android/app/build/outputs/apk/debug/app-debug.apk")
TEST_APK = os.path.join(REPO, "android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk")
RUNNER = "com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner"
SAVED, NOT_FOUND, WRONG_PSK, JOIN_FAILED = 0x04, 0x10, 0x11, 0x12


def adb(*a, secs=180):
    return subprocess.run(["adb", *a], capture_output=True, text=True, timeout=secs).stdout


def op(name, **kw):
    """one BleOp call; its JSON result (never printed whole: no values in it, but keep the log short)"""
    a = ["shell", "am", "instrument", "-w", "-r", "-e", "op", name]
    for k, v in kw.items():
        if str(v):                              # adb shell drops an empty word: '-e writes ""' would eat the next key
            a += ["-e", k, str(v)]
    m = re.search(r"INSTRUMENTATION_STATUS: result=(.*)", adb(*a, RUNNER, secs=240))
    res = json.loads(m.group(1)) if m else {"error": "no result from the phone"}
    return res


def hexs(s):
    return s.encode().hex() if isinstance(s, str) else s.hex()


def creds():
    """(ssid len, crc), (psk len, crc) as stored on the BL616; None for a missing key"""
    out = bl("pt_ble creds", 2, r"wifi_psk[^\n]*\n")
    got = []
    for key in ("wifi_ssid", "wifi_psk"):
        m = re.search(key + r" len (\d+) crc ([0-9a-f]{8})", out)
        got.append((int(m.group(1)), int(m.group(2), 16)) if m else None)
    return tuple(got)


def want(ssid, psk):
    return tuple((len(v.encode()), zlib.crc32(v.encode())) if v is not None else None for v in (ssid, psk))


def states(res):
    return [s["state"] for s in res.get("states", [])]


def ble_on():
    """pt_ble on, True once BLE says it advertises (or was already on)"""
    return bool(re.search(r"advertising as|already on", bl("pt_ble on", 5, r"advertising as|already on")))


def bl_back(what, secs=40):
    """after a BL616 reset: wait for the console, BLE on again. The board keeps no bond keys (CONFIG_BT_SETTINGS off,
    SECURITY.txt), so the phone forgets the board and pairs again (the app's 'Forget' case); probing the old bond
    first left a pairing dialog nobody pressed for 15 s (2026-10-28)"""
    t0 = time.time()
    while time.time() - t0 < secs and not os.path.exists(BL_PORT):
        time.sleep(0.5)
    time.sleep(2)
    bl("pt_off", 2)
    check(f"BLE started again after {what}", ble_on())
    op("scan", secs=20)
    op("unpair", address=addr)
    op("session", address=addr, pair="accept", writes="", secs=1)


def install(apk):
    """adb install, answering Play Protect's "verify this app" dialog on the phone (an install over USB waits for it):
    its "install without checking" button is tapped from a UI dump (2026-10-28, user: no hand on the phone)"""
    done = threading.Event()

    def press():
        while not done.wait(1):
            xml = adb("exec-out", "uiautomator", "dump", "/dev/tty", secs=20)
            if "com.android.vending" not in xml:
                continue
            m = re.search(r'text="[^"]*(?:без проверки|without (?:checking|scanning)|anyway|Не отправлять|Don.t send)[^"]*"'
                          r'[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml, re.I)
            if m:
                x1, y1, x2, y2 = map(int, m.groups())
                adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
                r.notes.append("Play Protect dialog answered at install")

    t = threading.Thread(target=press, daemon=True)
    t.start()
    adb("install", "-r", "-g", "-t", apk)       # -g: the Bluetooth permissions granted at install
    done.set()
    t.join()


def connects():
    """wifi_link.c's connect calls so far (pt_trace counts 0x62): a restart or rejoin adds one"""
    m = re.search(r"\b62=(\d+)", bl("pt_trace counts", 2, r"trace counts[^\n]*\n"))
    return int(m.group(1)) if m else 0


def link(want_ssid, secs=20):
    """seconds until the bridge is associated with want_ssid (None: until it is not associated), -1 if not in secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        out = bl("pt_wifi link", 2, r"pt_wifi link: [^\n]*\n")
        m = re.search(r'associated "(.*)" channel', out)
        if (m.group(1) if m else None) == want_ssid:
            return time.time() - t0
        time.sleep(0.5)
    return -1


# one runner at a time: a second one (2026-10-28: a stop by the wrong PID left run 6 going under runs 7 and 8) drives
# the same board and phone and both runs' results are void
_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test040: another run.py is running")

r = Run(__file__)
checks, times = [], {}


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


ssid, psk = wifi_credentials()
if "--flash" in sys.argv:
    subprocess.run(["make", "-C", os.path.join(REPO, "tools/power-bringup/bl616_powertest"), "flash"], check=True)
    time.sleep(5)
if "pt_ble" not in bl("help", 2):
    r.obstacle("the BL616 test firmware on the board has no pt_ble: flash it first (run.py --flash)")
    r.finish("not run: no pt_ble in the BL616 firmware.", False)
    sys.exit(1)
if "device" not in adb("get-state", secs=10):
    r.obstacle("no Android phone on adb (USB debugging on, the prompt accepted)")
    r.finish("not run: no phone on adb.", False)
    sys.exit(1)
for pkg, apk in (("com.leakcam.setup", APK), ("com.leakcam.setup.test", TEST_APK)):
    # only a changed APK is installed: every install over USB brings up Play Protect's "verify this app" (2026-10-28)
    on_phone = adb("shell", "pm", "path", pkg).strip().replace("package:", "")
    if not on_phone or adb("shell", "sha256sum", on_phone).split()[:1] != [hashlib.sha256(open(apk, "rb").read()).hexdigest()]:
        install(apk)
adb("shell", "svc", "power", "stayon", "usb")   # the screen stays on while plugged: the pairing dialog shows
adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
adb("shell", "cmd", "bluetooth_manager", "enable")
# the phone charges from the computer, not the other way round: a Samsung on a USB-C port came up as the power source
# and ran its battery down to 3 % during run 6 (2026-10-28)
if re.search(r"status=\{[^}]*?power_role=source", adb("shell", "dumpsys", "usb"), re.S):
    adb("shell", "dumpsys", "usb", "set-port-roles", "port0", "sink", "device")
    r.notes.append("phone USB port was the power source: swapped to sink")
# a locked phone shows the pairing dialog over the lock screen, where UI Automator cannot press it (run 6, 2026-10-28)
if "isKeyguardShowing=true" in adb("shell", "dumpsys", "window"):
    r.obstacle("the phone is locked: unlock it (screen lock off for the bench phone, or Developer options > Stay awake)")
    r.finish("not run: the phone is locked.", False)
    sys.exit(1)

r.off()
bl("pt_ble erase", 2)
r.sample("start")

# 1. advertising on USB power: time from pt_ble on to the phone's first sight of it
t0 = time.time()
check("BLE started (pt_ble on)", ble_on())
res = op("scan", secs=20)
times["advertising"] = time.time() - t0
check("advertises on USB power", res.get("found"), f"{res.get('name')} rssi {res.get('rssi')} after {times['advertising']:.1f} s")
if not res.get("found"):
    r.finish("the board was never seen advertising.", False)
    sys.exit(1)
addr = res["address"]
op("unpair", address=addr)

# 2. encryption gate: pairing cancelled -> every write refused, nothing stored
res = op("session", address=addr, pair="cancel", writes=f"ssid:{hexs('t040-gate')},commit:02", secs=5)
w = res.get("writes", [])
check("no write succeeds after a cancelled pairing", w and all(x["status"] != 0 for x in w),
      str([x["status"] for x in w]))           # the link drops with the cancel: -1, never delivered
check("nothing stored without pairing", creds() == (None, None))

# 3. pairing and the characteristic limits (MTU 247 as the app)
t1 = time.time()
lim = [("ssid", "s" * 32, True), ("ssid", "s" * 33, False), ("psk", "p" * 7, False), ("psk", "p" * 64, False),
       ("psk", "p" * 8, True), ("psk", "p" * 63, True), ("commit", bytes([3]), False),
       ("commit", bytes([1, 1]), False)]       # no zero-length writes: the SDK's ATT layer refuses them (0x13)
res = op("session", address=addr, pair="accept", writes=",".join(f"{c}:{hexs(v)}" for c, v, _ in lim), secs=3)
times["pair"] = time.time() - t1
check("paired (Just Works on USB power)", res.get("bond") == 12, f"{res.get('pair')}, MTU {res.get('mtu')}")
for (c, v, ok), x in zip(lim, res.get("writes", [])):
    check(f"{c} {len(v)} bytes {'accepted' if ok else 'refused'}", x["status"] == 0 if ok else x["status"] > 0,
          f"GATT {x['status']}")                # a refusal is the board's GATT error; -1 = never delivered
res = op("session", address=addr, pair="accept", writes="commit:02", secs=3)   # a new connection starts empty
check("commit without an SSID refused", res.get("writes") and res["writes"][0]["status"] > 0,
      f"GATT {res.get('writes', [{}])[0].get('status')}")
check("commit without an SSID stores nothing", creds() == (None, None))
res = op("session", address=addr, pair="accept", mtu=23, writes=f"ssid:{hexs('o' * 30)}", secs=3)   # long write: offsets
check("offset (long) write refused", res.get("writes") and res["writes"][0]["status"] > 0,
      f"GATT {res.get('writes', [{}])[0].get('status')}")

# 4. store unchecked (0x02), read back exactly; 6. overwrite with shorter values
for s_, p_ in (("t040-" + "L" * 27, "P" * 63), ("t040-s", "pass1234")):
    t1 = time.time()
    res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:02", secs=10)
    times.setdefault("store", time.time() - t1)
    check(f"store unchecked {len(s_)}/{len(p_)} bytes: SAVED", states(res) == [SAVED], str(states(res)))
    check(f"stored exactly {len(s_)}/{len(p_)} bytes", creds() == want(s_, p_))
mark = want("t040-s", "pass1234")

# 5. persistence: HBN sleep + wake, watchdog reset
bl("pt_sleep 3 nousb", 1)
time.sleep(6)
bl_back("HBN sleep and wake")
check("kept over HBN sleep and wake", creds() == mark)
bl("pt_hang", 1)
time.sleep(16)                                  # watchdog recovery 12.9 s (test038)
bl_back("a watchdog reset")
check("kept over a watchdog reset", creds() == mark)

# 7. checked store: failures keep the old credentials, the right ones are stored after JOINING -> ADDRESS -> INTERNET
bad = psk[:-1] + ("x" if psk[-1] != "x" else "y")
for name, s_, p_, final in (("wrong passphrase", ssid, bad, (WRONG_PSK, JOIN_FAILED)),
                            ("unknown SSID", "LEAKCAM-T040-NONE", psk, (NOT_FOUND,))):
    res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:01", secs=90)
    st = states(res)
    check(f"check, {name}: {final[0]:#x}", st and st[-1] in final, str(st))
    check(f"check, {name}: old credentials kept", creds() == mark)
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01", drop_at="01", secs=10)
time.sleep(60)                                  # the check runs on; the phone is gone
check("check with the link dropped: nothing stored", creds() == mark, str(states(res)))
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01,commit:01", secs=90)
st = states(res)
check("checked store: JOINING -> ADDRESS -> INTERNET -> SAVED", st == [1, 2, 3, SAVED], str(st))
check("a second commit during the check starts no second check", st.count(1) == 1)
check("checked store: the real network stored", creds() == want(ssid, psk))
times["check"] = [(s["state"], s["ms"]) for s in res.get("states", [])]

# 8. BLE first (user, 2026-10-28): a check takes the radio even from a K230 session, which gets it back; new
#    credentials stored during a session end it and a new one starts on them. The bridge stands in for the session
#    (pt_wifi bridge, the K230 not booted); link() reads its association without taking the radio
bl("pt_on", 2)
bl("pt_wifi bridge", 12, r"associated \+ WPA|not associated")
check("session: the bridge on the stored network", link(ssid) >= 0)
n = connects()
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(bad)},commit:01", secs=90)
st = states(res)
check("check during a session runs (BLE first): WRONG_PASSWORD", st[:1] == [1] and st[-1:] in ([WRONG_PSK], [JOIN_FAILED]),
      str(st))
check("check during a session: old credentials kept", creds() == want(ssid, psk))
t = link(ssid)
check("check during a session: the session rejoins", t >= 0 and connects() > n, f"{t:.1f} s after the result")
n = connects()
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01", secs=90)
check("checked store during a session: SAVED", states(res) == [1, 2, 3, SAVED], str(states(res)))
t = link(ssid)
check("checked store during a session: the session restarts on the network", t >= 0 and connects() > n,
      f"{t:.1f} s after SAVED")
times["restart"] = t
other = "LEAKCAM-T040-NEW"                      # a network that is not there: the restarted session must try it
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(other)},psk:{hexs('pass1234')},commit:02", secs=10)
check("store 0x02 during a session: SAVED", states(res) == [SAVED], str(states(res)))
t = link(None)
check("store 0x02 during a session: the session restarts on the new network (gone from the old one)", t >= 0)
time.sleep(15)                                  # the joins of the missing network fail through to autoconnect
n = connects()
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:02", secs=10)
t = link(ssid)
check("the real network stored again after a failed join: the session restarts on it", t >= 0 and connects() > n,
      f"{t:.1f} s")
times["after_missing"] = t
# the router's password changed (user, 2026-10-28): the session retries the old one (auth failures) when the phone
# sends the new one with a check - a connect into those retries hung the Wi-Fi task before wifi_link_sta_leave()
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(bad)},commit:02", secs=10)
check("password changed: the session restarts on the old one and fails", states(res) == [SAVED] and link(None) >= 0)
time.sleep(15)                                  # its auth retries run
n = connects()
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01", secs=90)
check("password changed: the check of the new one during the retries passes", states(res) == [1, 2, 3, SAVED],
      str(states(res)))
t = link(ssid)
check("password changed: the session back on the network", t >= 0 and connects() > n, f"{t:.1f} s after SAVED")
times["after_password"] = t
r.off()

# 9. repeats: 20 connect / store / disconnect cycles, BL616 memory after each (leak verdict)
fails = 0
for i in range(20):
    s_, p_ = (f"t040-rep{i}", f"repeat-{i:04d}")
    res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:02", secs=10)
    fails += states(res) != [SAVED] or creds() != want(s_, p_)
    r.sample(f"cycle {i + 1}")
check("20 store cycles", fails == 0, f"{fails} failed")
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:02", secs=10)
check("the real network stored again at the end", creds() == want(ssid, psk))
op("unpair", address=addr)

bad_ = [c for c in checks if not c[1]]
ck = times.get("check", [])
r.set("speed", f"advertising seen {times['advertising']:.1f} s after BLE start; connect + pair {times.get('pair', 0):.1f} s; "
               f"store 0x02 {times.get('store', 0):.1f} s per session; check 0x01 "
               + " -> ".join(f"{s:#x} {ms / 1000:.1f} s" for s, ms in ck)
               + f"; session back on the network {times.get('restart', -1):.1f} s after SAVED, after a missing network "
               f"{times.get('after_missing', -1):.1f} s, after a changed password {times.get('after_password', -1):.1f} s "
               f"({r.date}, run.py)")
r.set("boot", f"n/a: the K230 is not booted (BL616 only) ({r.date})")
r.set("voltage", f"n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) ({r.date})")
r.set("perday", f"n/a: on USB power only, once per installation ({r.date})")
r.finish(f"{len(checks) - len(bad_)} of {len(checks)} checks passed"
         + ("; failed: " + "; ".join(f"{n} ({d})" for n, _, d in bad_) if bad_ else ".")
         + " Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).",
         not bad_)
