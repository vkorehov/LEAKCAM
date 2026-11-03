#!/usr/bin/env python3
"""test117 automated: presence lost, for whatever reason, brings BLE back, so a phone nearby sets the Wi-Fi at once
(user 2026-11-01). On the product pair (firmware/bl616_full in slot 0, bl616_setup in slot 1; the full firmware has no
BLE): a full firmware whose check-in does not reach the server switches to the setup firmware ("lost") at once, which
advertises; each cause (the server down; the stored network gone: a missing one saved without checking from the app)
ends with the phone finding the board and setting the real network from the app, which switches it back to the full
firmware, present again. The setup firmware stays while the server is back (its retry is 6 h): only a setup ends it.
USB-C 1 in, the phone on adb, unlocked; nothing by hand.
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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import board_id, firmware, op, product, ready, to_firmware   # noqa: E402

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


def wait_fw(name, want, secs):
    """seconds until the board runs the want firmware (a switch of its own), checked; None if not in secs"""
    t = to_firmware(want, secs=secs)
    check(name, t is not None, f"{t:.0f} s" if t is not None else f"not within {secs} s: {firmware()}")
    return t


def found(name):
    """the phone's scan: the board (its serial) found, the RSSI logged (the phone may be rooms away)"""
    res = op("scan", secs=20)
    ok = res.get("found") and res.get("name") == dev
    check(name, ok, f"{res.get('name')} rssi {res.get('rssi')} after {res.get('ms', 0) / 1000:.1f} s")
    return ok


def online(dev):
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
            return json.loads(f.read()).get("online", False)
    except OSError:
        return False


def setup(s, k, anyway=False):
    """one setup through the app; a stored network (Done, Saved without checking) switches the board back to the full
    firmware: waited for here"""
    res = op("app", ssid=s.encode().hex(), psk=k.encode().hex(), anyway="1" if anyway else "", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    secs = res.get("ms_end", 0) / 1000
    log("setup", s if s != ssid else "<network>", f"{secs:.1f} s", last[:70].replace(ssid, "<network>"))
    if last.startswith("Done.") or last.startswith("Saved without checking."):
        wait_fw("stored: back in the full firmware", "full", 60)
    return last, secs


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
dev = board_id()
poll_interval(POLL_S)
t_lost = []
try:
    # 0. set up and present: the full firmware, no BLE
    to_firmware("setup", "switch_to_setup lost")
    last, secs = setup(ssid, psk)
    check("start: set up from the app", last.startswith("Done."), f"{secs:.1f} s")
    on_run_interval()
    check("present at the server", online(dev))
    res = op("scan", secs=15)
    check("present: the phone does not find the board (no BLE in the full firmware)", not res.get("found"),
          str(res.get("name")))

    # 1. the server down: the next check-in fails -> the setup firmware at once; it stays with the server back; the
    #    phone sets the network -> the full firmware, present
    pi("sudo systemctl stop leakcam-server")
    try:
        t = wait_fw("server down: the setup firmware (presence lost)", "setup", POLL_S + 60)
        if t is not None:
            t_lost.append(t)
            found("server down: the phone finds the board at once")
    finally:
        pi("sudo systemctl start leakcam-server")
    time.sleep(2 * POLL_S)
    check("server back: the setup firmware stays (only a setup or its 6 h retry ends it)", firmware() == "setup")
    t0 = time.time()
    last, secs = setup(ssid, psk)
    check("server back: the phone sets the network", last.startswith("Done."), f"{secs:.1f} s")
    while time.time() - t0 < 90 and not online(dev):
        time.sleep(1)
    check("server back: present at the server", online(dev), f"{time.time() - t0:.0f} s after the setup started")

    # 2. the stored network gone (a missing one saved without checking): the full firmware cannot check in -> the
    #    setup firmware at once -> the phone sets the real network -> present, no BLE again
    to_firmware("setup", "switch_to_setup lost")
    last, secs = setup(GONE, psk, anyway=True)
    check("network gone: saved without checking", last.startswith("Saved without checking."), last[:70])
    t = wait_fw("network gone: the setup firmware (presence lost)", "setup", 90)
    if t is not None:
        t_lost.append(t)
        found("network gone: the phone finds the board at once")
    t0 = time.time()
    last, secs = setup(ssid, psk)
    check("network gone: the phone sets the real network at once", last.startswith("Done."), f"{secs:.1f} s")
    while time.time() - t0 < 90 and not online(dev):
        time.sleep(1)
    check("network back: present at the server", online(dev), f"{time.time() - t0:.0f} s after the setup started")
    res = op("scan", secs=15)
    check("network back: no BLE again", not res.get("found"), str(res.get("name")))
    r.set("speed", "presence lost -> setup firmware " + ", ".join(f"{t:.0f} s" for t in t_lost)
          + f" (poll {POLL_S} s); set up again from the app {secs:.1f} s")
    r.sample("after the checks")
finally:
    poll_interval(60)

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
