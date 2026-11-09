#!/usr/bin/env python3
"""test116: the whole setup over BLE alone, no USB, on the product pair (firmware/bl616_full in slot 0, bl616_setup in
slot 1): a board with no network on its cell runs the setup firmware asleep in PDS, advertising every 2 s; the phone
finds it, the app sets it up on battery, the stored network switches it to the full firmware (no BLE); configured, it
is silent on battery; erased on battery, the full firmware starts the setup firmware without USB (the path that
wedged the BLE core, 2026-11-03) and it is set up again; with USB-C 1 back in it stays the full firmware, silent. No hands: the bench relay box takes USB-C 1 out and puts
it back (the cell stays in; phone.hand() -> the relay app com.relay); without the box a hand step waits for HAND_FILE
(`echo unplugged > ~/.leakcam_hand`).
    tests/test116-ble-only-setup/run.py [--unplugged]   --unplugged: no network stored and USB-C 1 already out (the
                                                         setup firmware asleep: no console), from step 2's scan on;
                                                         the board's ID in LEAKCAM_DEV (no console to read it)
"""
import fcntl
import os
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import erase, board_id, creds, usb_seen, firmware, hand as ask, online, only_boards, op, ready, to_firmware, want   # noqa: E402


_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test116: another run.py is running")

r = Run(__file__)
checks, times = [], {}


class Stop(Exception):
    """a step failed: the later steps depend on it"""


def check(name, ok, detail="", stop=False):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)
    if stop and not ok:
        raise Stop(name)


def hand(action, word):
    t = ask(action, word)
    if t is None:
        raise Stop(f"no hand step: {action}")
    return t


def found(name, t0, key, secs=60):
    """the phone's scan until the board (its serial) shows; the time from t0"""
    while time.time() - t0 < secs:
        res = op("scan", secs=20)
        if res.get("found") and res.get("name") == dev:
            times[key] = time.time() - t0
            check(name, True, f"rssi {res.get('rssi')}, {times[key]:.1f} s")
            return
    check(name, False, f"not within {secs} s", stop=True)


def setup_on_battery(what):
    """the app sets the network on battery: Done, the full firmware, present at the server"""
    only_boards()   # Setup lists new boards only (DESIGN.md 5.3.4): this one off the phone's list first
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="", secs=45)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    times.setdefault("setup", []).append(res.get("ms_end", 0) / 1000)
    check(f"{what}: set up from the app on battery (PIN, no dialog)", last.startswith("Done."),
          f"{times['setup'][-1]:.1f} s, pairing {res.get('pairing')}", stop=True)
    t = to_firmware("full", secs=30)
    check(f"{what}: the full firmware", t is not None, f"{t:.0f} s after Done" if t else str(firmware()), stop=True)
    check(f"{what}: the network stored", creds() == want(ssid, psk))
    t0 = time.time()
    while time.time() - t0 < 30 and not online(dev):
        time.sleep(0.3)
    check(f"{what}: present at the server", online(dev), f"{time.time() - t0:.0f} s after the switch")


ssid, psk = wifi_credentials()
UNPLUGGED = "--unplugged" in sys.argv
if not UNPLUGGED and not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on PR1: reconnect, then run again")
    r.finish("not run: no BL616 console.", False, switch_off=False)
    sys.exit(1)
if not UNPLUGGED and firmware() not in ("full", "setup"):
    r.obstacle(f"the BL616 runs {firmware()}: flash the product pair")
    r.finish("not run: not the product pair.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
dev = board_id() or os.environ.get("LEAKCAM_DEV", "")   # --unplugged: no console, the ID given
try:
    # 1. a new board: no network; the full firmware switches to the setup firmware by itself
    if not UNPLUGGED:
        erase()
        check("no network stored", creds() == (None, None), stop=True)
        t = to_firmware("setup", secs=90)   # the full firmware sees the erase at its next check-in (31 s measured, test116)
        check("no network: the setup firmware", t is not None, f"{t:.0f} s" if t else str(firmware()), stop=True)

    # 2. on its cell: USB-C 1 out while awake: back to 2 s advertising and the sleep (setup_sleep.c's unplug path)
    t_unplug = time.time() if UNPLUGGED else hand("unplug USB-C 1 (the cell stays in)", "unplugged")
    found("unplugged: the phone finds the board asleep", t_unplug, "found_unplug")
    setup_on_battery("unplugged")

    # 3. configured, on battery: no BLE
    res = op("scan", secs=8)   # silent: 4 battery advertising intervals (2 s) are enough, user 2026-11-09 "phone idle"
    check("configured, on battery: silent (8 s scan)", not res.get("found"), str(res.get("name")))

    # 4. erased on battery: the full firmware starts the setup firmware without USB (BLE started before any sleep:
    #    setup_boot.c), found and set up again
    erase()
    t_erase = time.time()
    found("battery start: the phone finds the setup firmware", t_erase, "found_start", secs=120)
    setup_on_battery("battery start")

    # 5. USB-C 1 back in: still the full firmware, still silent, still present
    t_plug = hand("plug USB-C 1 back in", "plugged")
    usb_seen(True)                      # the board's own plug edge (no fixed 5 s)
    check("USB back in: the full firmware", firmware() == "full", str(firmware()))
    res = op("scan", secs=8)
    check("USB back in: silent", not res.get("found"), str(res.get("name")))
    check("USB back in: present at the server", online(dev), f"{time.time() - t_plug:.0f} s after the plug")
    r.set("speed", f"on battery: found {times['found_unplug']:.1f} s after the unplug, "
                   f"{times['found_start']:.1f} s after the erase (the switch to the setup firmware); app opened -> "
                   f"Done {', '.join(f'{t:.1f}' for t in times['setup'])} s ({r.date})")
    r.sample("after the checks")
except Stop as e:
    stopped = str(e)
    log("stopped at:", e)
else:
    stopped = ""

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
if stopped:   # the later steps never ran (a missing hand step too): not a pass
    summary += f" Stopped at: {stopped}."
ok = not failed and not stopped
r.finish(summary, ok, switch_off=False)
sys.exit(0 if ok else 1)
