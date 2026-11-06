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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import adb, firmware, only_boards, op, phone_boards, product, ready, register, to_firmware   # noqa: E402

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
    detail = str(detail).replace(f'"{ssid}"', '"<network>"') if ssid else detail   # the network name never in a log
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def server(path, form=None):
    """a GET, or a POST of the form (bytes) as a board's check-in"""
    with urllib.request.urlopen(SERVER + path, data=form, timeout=5) as f:
        return json.loads(f.read())


def poll_interval(s):
    # only the poll line: the env file also holds the pairing PIN key (server/deploy.sh); overwriting it whole left the
    # server without the key (2026-11-01)
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, f"sed -i 's/^LEAKCAM_POLL_S=.*/LEAKCAM_POLL_S={s}/' "
                    "~/leakcam-server/env && sudo systemctl restart leakcam-server"], check=True, timeout=30)
    time.sleep(1)


def app_mine():
    """the app reopened: its presence lines from logcat ("mine <id>: <line>"), per ID, polled until every LEAKCAM in
    its list (the server's for this phone) has one (a fixed 6 s missed a slow first refresh, 2026-11-01)"""
    listed = set(phone_boards())
    adb("logcat", "-c")
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    t0, lines = time.time(), {}
    while time.time() - t0 < 15:
        time.sleep(1)
        lines = dict(re.findall(r"mine ([0-9A-F]{12}): (.*)", adb("logcat", "-d", "-s", "LeakcamSetup:I")))
        if listed <= set(lines):
            break
    return lines


def add_to_app(dev_id, only=False):
    """a board in this phone's list as a setup leaves it: registered at the server (DESIGN.md 5.3.5: the app keeps no
    list); only: the list holds just this one (the run's starting state, whatever a stopped run left)"""
    adb("shell", "am", "force-stop", PKG)
    if only:
        only_boards(dev_id)
    else:
        register(dev_id)


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if not product():
    r.obstacle("the BL616 runs the test firmware: flash the product (firmware/bl616_full, make flash)")
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
    add_to_app(dev, only=True)   # starting state: the app knows this board, nothing else
    adb("logcat", "-c")
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=30)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    check("setup Done", last.startswith("Done."), f"{res.get('ms_end', 0) / 1000:.1f} s")
    logcat = adb("logcat", "-d", "-s", "LeakcamSetup:I")
    system = adb("logcat", "-d")
    check("pairing: the app entered the PIN, no PIN prompt on the phone", "pairing PIN entered by the app" in logcat
          and "bluetooth.BluetoothPairingDialog" not in system,
          f"dialog started {system.count('cmp=com.android.settings/.bluetooth.BluetoothPairingDialog')}x")
    found = re.findall(r"found \S+ id ([0-9A-F]{12}|none)", logcat)
    check("BLE advertisement = the device ID", dev in found, str(found))
    if res.get("status_screen"):         # the app knew the board already: its short flow, the network shown hidden
        check("known board: the status screen opened its setup", True, f"{res.get('ms_status', 0) / 1000:.1f} s")
        check("known board: the short 'change its Wi-Fi' flow", any("Enter the new Wi-Fi network for" in h["help"]
                                                                      for h in helps), helps[0]["help"][:50] if helps else "")
    check("advertisement: a stored network flagged", re.search(rf"found \S+ id {dev} creds 1", logcat) is not None,
          (re.findall(rf"found \S+ id {dev} creds (\d)", logcat) or ["?"])[-1])
    prefs = adb("shell", "run-as", PKG, "ls", "shared_prefs")
    check("the app stores no list (the server's)", "devices.xml" not in prefs, prefs.strip() or "no prefs")
    check("the server lists the board for this phone, by its serial", dev in phone_boards(), str(phone_boards()))

    # 2. the board checks in at the server: online within two intervals after the setup (the phone left)
    t0, online = time.time(), False
    while time.time() - t0 < 2 * 60 + 10 and not online:   # its first interval is the default 60 s until told
        online = server(f"/v1/devices/{dev}").get("online", False)
        if not online:
            time.sleep(3)
    check("server: the board online after the setup", online, f"{time.time() - t0:.0f} s")
    r.set("speed", f"setup Done -> online at the server {time.time() - t0:.0f} s (poll {POLL_S} s, the board's first "
          f"interval {60} s until told)")
    # its own answer, asked again when the Wi-Fi logs drown it (2026-11-04: the prompt was read instead)
    st = next((o for o in (bl("presence", 3, r"last check-in state 0x[0-9a-f]{2}") for _ in range(3)) if re.search(r"last check-in state 0x", o)), "")
    m = re.search(r"state 0x[0-9a-f]{2}", st)
    check("board: last check-in reached the server (0x03)", "state 0x03" in st, m.group(0) if m else "no answer")

    # 3. the app remembers it and shows it online
    lines = app_mine()
    check("app: the board listed and online", "online" in lines.get(dev, ""), lines.get(dev))
    check("app: the board's network shown (the SSID from its check-in)", f'Wi-Fi "{ssid}"' in lines.get(dev, ""),
          lines.get(dev))

    # 4. a second LEAKCAM: listed, online while it checks in, offline when it stops
    add_to_app(SECOND)
    lines = app_mine()
    check("app: second LEAKCAM listed, not seen yet", "not seen yet" in lines.get(SECOND, ""), lines.get(SECOND))
    server(f"/v1/poll?id={SECOND}", b"ssid=t111-second")   # as a board reports its network
    lines = app_mine()
    check("app: second LEAKCAM online after its check-in", ": online" in lines.get(SECOND, ""), lines.get(SECOND))
    check("app: second LEAKCAM's network shown", 'Wi-Fi "t111-second"' in lines.get(SECOND, ""), lines.get(SECOND))
    check("app: the board still online next to it", ": online" in lines.get(dev, ""), lines.get(dev))
    time.sleep(3 * POLL_S + 2)                               # three check-ins missed
    lines = app_mine()
    check("app: second LEAKCAM offline when it stopped", "offline" in lines.get(SECOND, ""), lines.get(SECOND))
    check("app: the board online throughout", ": online" in lines.get(dev, ""), lines.get(dev))

    # 5. deleted from the phone: gone from the list, the board untouched
    res = op("delete", id=SECOND, secs=30)
    lines = app_mine()
    check("app: second LEAKCAM deleted", res.get("confirmed") and SECOND not in lines, str(sorted(lines)))
    check("app: the board still listed after the delete", ": online" in lines.get(dev, ""), lines.get(dev))

    # 6. the board itself deleted: it stays out, also after the app is reopened - someone else may still use it, a
    #    deleted board comes back only through a new setup (user 2026-11-04; DESIGN.md 5.3.5)
    def phones():
        """the server's registered phones (users) of the board: GET /v1/devices/<id> "phones" (DESIGN.md 5.3.5)"""
        try:
            with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
                return json.loads(f.read()).get("phones")
        except OSError:
            return None
    n = phones()
    check("server: this phone registered as a user (when it was issued the PIN)", isinstance(n, int) and n >= 1, str(n))
    adb("logcat", "-c")
    res = op("delete", id=dev, secs=30)
    check("server: the delete reached it first, no user left", phones() == 0, str(phones()))
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")
    time.sleep(15)
    log_ = adb("logcat", "-d", "-s", "LeakcamSetup:I")
    check("app: the board deleted", bool(res.get("confirmed")) and f"deleted {dev}" in log_, str(res))
    check("app: a deleted board stays out, also after a reopen", f"rediscovered {dev}" not in log_, "15 s after the reopen")
    # the only user gone (the bench is the board's one phone): the server answers its check-in 410 Gone and the board
    # goes back to the setup firmware within one check-in interval (DESIGN.md 5.3.5)
    t = to_firmware("setup", secs=POLL_S + 60)
    check("no user left: the board back in the setup firmware by itself", t is not None,
          f"{t:.0f} s after the delete" if t else str(firmware()))
    if t is not None:                          # set up again from the app: the bench as it was (the full firmware)
        op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=30)
        check("set up again after it: the full firmware", to_firmware("full", secs=30) is not None, str(firmware()))
    r.sample("after the checks")
finally:
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
