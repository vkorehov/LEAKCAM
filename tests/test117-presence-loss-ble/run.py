#!/usr/bin/env python3
"""test117 automated: presence lost, for whatever reason, brings BLE back, so a phone nearby sets the Wi-Fi at once
(user 2026-11-01). On the product firmware with a network stored and present: on battery it would not advertise. Each
cause of a lost presence (the server down; the stored network gone: a missing one saved without checking from the app)
must switch the battery rule to advertising at once (`prov`: "on battery on (presence lost)"), and the board is found
and set up from the app at once; present again, the rule goes back off. On USB power the board would advertise anyway:
`prov battery on` makes it follow the battery rule on USB power too, so the phone sees the radio as on battery (a
present board silent, a lost one found). USB-C 1 in, the phone on adb, unlocked; nothing by hand.
    tests/test117-presence-loss-ble/run.py
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import board_id, op, product, ready   # noqa: E402

SERVER = "http://192.168.1.159:8090"
PI = "pi@192.168.1.159"
POLL_S = 10                      # for the run; the server's default is 60
GONE = "t117-gone"               # a network that is not there

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test117: another run.py is running")

r = Run(__file__)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def pi(cmd):
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, cmd], check=True, timeout=30)


def poll_interval(s):
    # only the poll line: the env file also holds the pairing PIN key (server/deploy.sh)
    pi(f"sed -i 's/^LEAKCAM_POLL_S=.*/LEAKCAM_POLL_S={s}/' ~/leakcam-server/env && "
       "sudo systemctl restart leakcam-server")
    time.sleep(1)


def board_poll():
    """the board's check-in interval as it took it from the server's last answer"""
    m = re.search(r"poll (\d+) s", bl("presence", 2, r"poll \d+ s"))
    return int(m.group(1)) if m else 0


def on_run_interval():
    """the board on the run's interval (it takes it from a check-in answer, up to one old interval later)"""
    t0 = time.time()
    while time.time() - t0 < 75 and board_poll() != POLL_S:
        time.sleep(2)
    check(f"the board checks in every {POLL_S} s", board_poll() == POLL_S,
          f"poll {board_poll()} s after {time.time() - t0:.0f} s")


def rule():
    m = re.search(r"on battery (on \([^)]*\)|off \([^)]*\))", bl("prov", 2, r"on battery (on|off)"))
    return m.group(1) if m else "?"


def wait_rule(name, want, secs):
    t0, got = time.time(), "?"
    while time.time() - t0 < secs:
        got = rule()
        if got.startswith(want):
            check(name, True, f"{time.time() - t0:.0f} s: {got}")
            return time.time() - t0
        time.sleep(1)
    check(name, False, f"not within {secs} s: {got}")
    return None


def online(dev):
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
            return json.loads(f.read()).get("online", False)
    except OSError:
        return False


def setup(s, k, anyway=False):
    res = op("app", ssid=s.encode().hex(), psk=k.encode().hex(), anyway="1" if anyway else "", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    log("setup", s if s != ssid else "<network>", f"{res.get('ms_end', 0) / 1000:.1f} s", last[:70])
    return last, res.get("ms_end", 0) / 1000


ssid, psk = wifi_credentials()
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB: reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if not product():
    r.obstacle("the BL616 runs the test firmware: flash the product (firmware/bl616, make flash)")
    r.finish("not run: not the product firmware.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id()
poll_interval(POLL_S)
try:
    # 0. set up and present: the battery rule off
    last, secs = setup(ssid, psk)
    check("start: set up from the app", last.startswith("Done."), f"{secs:.1f} s")
    wait_rule("present: no advertising on battery", "off (configured, present)", 2 * POLL_S + 20)
    on_run_interval()
    bl("prov battery on", 2, r"battery rule")
    res = op("scan", secs=15)
    check("present: the phone does not find the board", not res.get("found"), str(res.get("name")))

    # 1. the server down: presence lost at the next check-in -> advertising at once; back up -> off again
    pi("sudo systemctl stop leakcam-server")
    try:
        t = wait_rule("server down: advertising on battery (presence lost)", "on (presence lost)", POLL_S + 30)
        if t is not None:
            res = op("scan", secs=20)
            check("server down: the phone finds the board at once", res.get("found") and res.get("name") == dev,
                  f"{res.get('name')} after {res.get('ms', 0) / 1000:.1f} s")
    finally:
        pi("sudo systemctl start leakcam-server")
    wait_rule("server back: advertising off again", "off (configured, present)", POLL_S + 30)

    # 2. the stored network gone (a missing one saved without checking): lost at once -> the phone sets the real
    #    network at once -> present, off again
    bl("prov battery off", 2, r"battery rule")    # present and silent on battery: the save itself over USB power
    last, secs = setup(GONE, psk, anyway=True)
    bl("prov battery on", 2, r"battery rule")
    check("network gone: saved without checking", "without checking" in last.lower() or last.startswith("Saved"),
          last[:70])
    wait_rule("network gone: advertising on battery (presence lost)", "on (presence lost)", 60)
    t0 = time.time()
    last, secs = setup(ssid, psk)
    check("network gone: the phone sets the real network at once", last.startswith("Done."), f"{secs:.1f} s")
    wait_rule("network back: advertising off again", "off (configured, present)", 30)
    while time.time() - t0 < 60 and not online(dev):
        time.sleep(1)
    check("network back: present at the server", online(dev), f"{time.time() - t0:.0f} s after the setup started")
    time.sleep(2)
    res = op("scan", secs=15)
    check("network back: silent again on battery", not res.get("found"), str(res.get("name")))
    r.set("speed", f"presence lost -> advertising: see the CHECK lines; set up again from the app {secs:.1f} s")
    r.sample("after the checks")
finally:
    bl("prov battery off", 2)
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
