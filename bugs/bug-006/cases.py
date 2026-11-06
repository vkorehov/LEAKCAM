#!/usr/bin/env python3
"""bug-006 follow-up cases on the setup test bed (tests/firmware/bl616_setup_test), USB-C 1 in, no hands: each runs in
a `wakeup_timer <ms> usb` sleep window and ends in a reboot + `lp_boot` with BLE found by the phone (no wedge left).
    bugs/bug-006/cases.py [gone] [connects] [nopin]     (all three when none named)
gone      the phone's Bluetooth off mid-connection while the board sleeps: advertising again, sleeps go on, found again
connects  many sessions in one window: sleeps vs vetoes, no wedge accumulated
nopin     no pairing PIN: BLE never started, the window sleeps; PIN back (provision_pin.py --force), BLE healthy
"""
import os
import re
import subprocess
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import REPO, bl   # noqa: E402
from board import log   # noqa: E402
from phone import adb, board_pin, firmware, op, ready   # noqa: E402

results = []
PIN = ""


def check(name, ok, detail=""):
    results.append((name, bool(ok)))
    log("CHECK", "pass" if ok else "FAIL", name, detail)
    return ok


def stat():
    """lp_sleepstat: sleeps entered / exited and BLE vetoes so far"""
    for _ in range(3):
        out = bl("lp_sleepstat", 2, r"after wake max \d+")
        m = re.search(r"sleeps entered (\d+), BLE idle vetoes (\d+).*?sleeps exited (\d+)", out, re.S)
        if m:
            return dict(zip(("entered", "vetoed", "exited"), map(int, m.groups())))
    return None


def delta(a, b):
    return {k: b[k] - a[k] for k in a} if a and b else None


def boot(why):
    """reboot + lp_boot: BLE ready (or none without a PIN); the phone's address of the board"""
    bl("reboot", 1)
    time.sleep(3)
    out = bl("lp_boot", 30, r"ble: ready as \S+|no pairing PIN|boot done")
    if "ble: ready" not in out:
        out += bl("", 15, r"ble: ready as \S+")
    m = re.search(r"ble: ready as (\S+)", out)
    if not check(f"{why}: BLE ready after reboot + lp_boot", m, "(a wedge never gets here)"):
        return None
    name = m.group(1).strip()
    t0 = time.time()
    while time.time() - t0 < 30:
        res = op("scan", secs=10)
        if res.get("found") and res.get("name") == name:
            check(f"{why}: the phone finds it", True, f"rssi {res.get('rssi')}")
            op("unpair", address=res["address"])     # the board keeps no bonds: a fresh pairing each boot
            global PIN
            PIN = board_pin()                          # now: in a sleep window the USB console (the ID) is off
            return res["address"]
    check(f"{why}: the phone finds it", False, "not in 30 s")
    return None


def window(ms):
    bl(f"wakeup_timer {ms} usb", 1)
    return time.time() + ms / 1000


def wait_until(t):
    while time.time() < t + 2:
        time.sleep(0.5)


def sleeps_ok(why, d, vetoes_max=None):
    check(f"{why}: sleeps in the window", d and d["entered"] > 0 and d["exited"] == d["entered"], str(d))
    if vetoes_max is not None:
        check(f"{why}: vetoes <= {vetoes_max}", d and d["vetoed"] <= vetoes_max, str(d and d["vetoed"]))


def case_gone():
    addr = boot("gone, start")
    if not addr:
        return
    s0, end = stat(), window(60000)
    res = {}
    t = threading.Thread(target=lambda: res.update(op("session", address=addr, secs=40, pin=PIN)))
    t.start()
    t_conn = time.time()
    time.sleep(14)                                   # connected, paired, waiting for STATUS
    adb("shell", "cmd", "bluetooth_manager", "disable", secs=20)
    t.join(60)
    log("gone: session " + str({k: res.get(k) for k in ("error", "connect_ms", "pair", "bond", "ccc", "disconnected_ms")}))
    check("gone: the session was up when the phone left", res.get("connect_ms") is not None,
          f"connect {res.get('connect_ms')} ms, bond {res.get('bond')}")
    time.sleep(10)                                   # the board's supervision timeout, then advertising again
    adb("shell", "cmd", "bluetooth_manager", "enable", secs=20)
    time.sleep(3)
    t0, found = time.time(), None
    while time.time() - t0 < 30 and not found:
        r = op("scan", secs=10)
        found = r.get("found") and r.get("address") == addr and time.time() - t0
    check("gone: advertising again after the phone left", found, f"{found:.1f} s after Bluetooth on" if found else "")
    log("gone: board " + " | ".join(l for l in bl("lp_sleepstat", 2, r"ble: never[^\n]*\n").splitlines() if "ble:" in l))
    wait_until(end)
    sleeps_ok("gone", delta(s0, stat()))
    log(f"gone: window {time.time() - t_conn:.0f} s")
    boot("gone, after")


def case_connects(n=6):
    addr = boot("connects, start")
    if not addr:
        return
    s0, end = stat(), window(150000)
    ok = 0
    for i in range(n):
        r = op("session", address=addr, secs=1, pin=PIN)
        log(f"connects: session {i + 1}: " + str({k: r.get(k) for k in ("error", "connect_ms", "pair", "bond", "ccc", "disconnected_ms")}))
        ok += r.get("connect_ms") is not None and r.get("ccc") == 0
        if time.time() > end - 15:
            break
    check(f"connects: sessions in one window", ok == i + 1, f"{ok}/{i + 1}")
    wait_until(end)
    sleeps_ok("connects", delta(s0, stat()))
    boot("connects, after")


def case_nopin():
    bl("pt_ble pin none", 2, r"pairing PIN erased|pin erase failed")
    bl("reboot", 1)
    time.sleep(3)
    out = bl("lp_boot", 30, r"boot done")
    out += bl("", 3, r"ble: ready")
    check("nopin: no BLE started", "boot done" in out and "ble: ready" not in out)
    s0, end = stat(), window(20000)
    wait_until(end)
    d = delta(s0, stat())
    sleeps_ok("nopin", d, vetoes_max=0)
    pwr = bl("lp_pwr", 2, r"Predict current: \d+uA")
    m = re.search(r"Total time (\d+)ms.*?PDS sleep: (\d+)ms", pwr, re.S)
    check("nopin: >= 95 % of the window in PDS", m and int(m.group(2)) >= 0.95 * int(m.group(1)),
          f"{m.group(2)} of {m.group(1)} ms" if m else pwr)
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools/power-bringup/provision_pin.py"), "--force"],
                       capture_output=True, text=True)
    check("nopin: PIN stored again", r.returncode == 0, r.stdout.strip().splitlines()[-1:] or r.stderr)
    boot("nopin, after")


if firmware() != "setup_test":
    sys.exit(f"cases.py: the board runs {firmware()}: flash tests/firmware/bl616_setup_test")
why = ready([])
if why:
    sys.exit(f"cases.py: {why}")
want = [a for a in sys.argv[1:] if not a.startswith("-")] or ["gone", "connects", "nopin"]
for c in want:
    log(f"=== case {c}")
    {"gone": case_gone, "connects": case_connects, "nopin": case_nopin}[c]()
failed = [n for n, ok in results if not ok]
log(f"cases.py: {len(results) - len(failed)}/{len(results)} checks pass" + (f"; FAIL: {'; '.join(failed)}" if failed else ""))
sys.exit(1 if failed else 0)
