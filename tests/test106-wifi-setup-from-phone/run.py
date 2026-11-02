#!/usr/bin/env python3
"""test106 automated: the LEAKCAM Setup app itself driven on the bench phone (BleOp op app: Find, the network, Pair,
check and save, the pairing dialogs) against the BL616 product pair (firmware/bl616_full in slot 0, bl616_setup in
slot 1: all setup in the setup firmware, every stored network switches the board back to the full firmware) or the
old test firmware; what the board stored checked on its console (`provision` / `pt_ble creds`: length + CRC-32, never
the passphrase). Needs USB-C 1 in, the phone on adb (Wi-Fi), unlocked, Bluetooth on.
README Steps 1, 3, 4, 5, 6 and 8; step 2 (wakes on battery, the K230 session) needs the rewritten product firmware,
step 7 a hand on the cable (test040 hand_steps.py usb-gate covers the board side).
    tests/test106-wifi-setup-from-phone/run.py
"""
import fcntl
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import creds, op, product, ready, want   # noqa: E402

REPEATS = 8                                     # the leak verdict's minimum (mem_leak.py)
SWITCH_WAIT_S = 60                              # setup -> full: the phone leaves (<= 15 s), then the slot switch

_lock = open(__file__)
try:
    fcntl.flock(_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit("test106: another run.py is running")

r = Run(__file__)
checks, times = [], {}


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def setup(ssid, psk, anyway=False):
    """one setup through the app; (last help text, seconds from opening the app to it, the op's result)"""
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="1" if anyway else "", secs=150)
    helps = res.get("help", [])
    last = helps[-1]["help"] if helps else res.get("error", "no help text")
    log("setup", ssid if ssid != wifi_ssid else "<network>", f"{res.get('ms_end', 0) / 1000:.1f} s",
        {k: res[k] for k in ("error", "pairing", "save_anyway", "ms_anyway", "ms_found") if k in res},
        "help at " + ",".join(f"{h['ms'] / 1000:.1f}" for h in helps),
        " | ".join(h["help"][:70].replace("\n", " ").replace(wifi_ssid, "<network>") for h in helps[-3:]))
    if PRODUCT and (last.startswith("Done.") or last.startswith("Saved without checking.")):
        back = to_firmware("full")
        check("stored: back in the full firmware", back is not None, f"{back:.1f} s" if back else "")
        times.setdefault("back", []).append(back or 0)
    return last, res.get("ms_end", 0) / 1000, res


def firmware():
    """'full' or 'setup' from the image `slot` names, or None; asked again when the full firmware's Wi-Fi logs drown
    the answer (test126 which())"""
    for _ in range(5):
        m = re.search(r"leakcam-build (\S+)", bl("slot", 3, r"leakcam-build \S+"))
        if m:
            return "full" if m.group(1) == "full" else "setup"
    return None


def to_firmware(want_fw, cmd=None):
    """seconds until want_fw answers (switched by cmd when another firmware runs, or by the board itself), or None"""
    t0 = time.time()
    while time.time() - t0 < SWITCH_WAIT_S:
        fw = firmware()                         # bl() waits up to 30 s for a port gone in a switch
        if fw == want_fw:
            return time.time() - t0
        if fw and cmd:
            bl(cmd, 1)
            cmd = None
        time.sleep(2)
    return None


def ble_on():
    """True once BLE advertises: the product's setup firmware advertises from its start (switched there from the full
    firmware); the test firmware by `pt_ble on` (`pt_ble` says "on" when it already was)"""
    if PRODUCT:
        return to_firmware("setup", "switch_to_setup lost") is not None
    return "ble: ready as" in bl("pt_ble on", 5, r"ble: ready as|already on") or "pt_ble: on" in bl("pt_ble", 2)


ssid, psk = wifi_credentials()
wifi_ssid = ssid
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB (board or the VM's USB hub gone): reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
PRODUCT = product()
log("BL616 firmware:", "product (firmware/bl616_common)" if PRODUCT else "test (bl616_powertest)")
if not PRODUCT and "pt_ble" not in bl("help", 2):
    r.obstacle("the BL616 test firmware on the board has no pt_ble: flash tests/firmware/bl616_powertest")
    r.finish("not run: no pt_ble in the BL616 firmware.", False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False)
    sys.exit(1)
check("BLE advertising on USB power", ble_on())

# 1. first setup: nothing stored, the app from opening to Done
bl("provision erase" if PRODUCT else "pt_ble erase", 2)   # in the setup firmware: it stays there
check("nothing stored before the first setup", creds() == (None, None))
last, secs, res = setup(ssid, psk)
times["first"] = secs
check("first setup: Done", last.startswith("Done."), f"{secs:.1f} s, pairing {res.get('pairing')}")
check("first setup: the network stored", creds() == want(ssid, psk))
r.sample("after the first setup")

# 3. failures are named and store nothing: the stored network stays
ble_on()
last, secs, res = setup(ssid, psk + "x")
check("wrong password named", "refused the password" in last, f"{secs:.1f} s")
check("wrong password: no Save without checking", not res.get("save_anyway"))
check("wrong password: the old network kept", creds() == want(ssid, psk))
gone = "t106-not-here"
last, secs, res = setup(gone, "password1")
check("network not seen named", "cannot see" in last, f"{secs:.1f} s")
check("network not seen: Save without checking offered", res.get("save_anyway"))
check("network not seen: the old network kept", creds() == want(ssid, psk))

# 4. + 5. save without checking replaces the stored network; a checked setup replaces it again
last, secs, res = setup(gone, "password1", anyway=True)
check("save without checking: said so", last.startswith("Saved without checking."), f"{secs:.1f} s")
check("save without checking: the new network stored", creds() == want(gone, "password1"))
ble_on()                                        # the full firmware loses the bogus network: back to setup
last, secs, res = setup(ssid, psk)
check("change back: Done", last.startswith("Done."), f"{secs:.1f} s")
check("change back: the first network stored again", creds() == want(ssid, psk))

# 8. repeats: every setup Done, BL616 memory steady (a sample after each: the harness's mem_leak.py verdict)
done = 0
for i in range(REPEATS):
    ble_on()
    last, secs, res = setup(ssid, psk)
    done += last.startswith("Done.")
    times.setdefault("repeat", []).append(secs)
    if not PRODUCT:   # the product pair resets after every setup (the switch back): no heap carries over, and fresh
        r.sample(f"repeat {i + 1}")   # boots read 194-210 KB by their timing, a false leak (2026-11-02)
check(f"{REPEATS} setups in a row: every one Done", done == REPEATS, f"{done}/{REPEATS}")

# 6. the board reset (a watchdog reset), after the repeats: a reset just before them made their first memory
#    sample a fresh heap, a false leak (2026-10-30): no bond on the board, the app pairs anew by itself
if PRODUCT:                                     # a reset in the setup firmware (the full one has no BLE)
    ble_on()
bl("reboot" if PRODUCT else "pt_hang", 1)
time.sleep(4 if PRODUCT else 16)                # watchdog recovery 12.9 s (test038)
t0 = time.time()
while time.time() - t0 < 40 and not os.path.exists(BL_PORT):
    time.sleep(0.5)
time.sleep(2)
if not PRODUCT:
    bl("pt_off", 2)
check("BLE started again after a reset", ble_on())
last, secs, res = setup(ssid, psk)
check("after the reset: Done with no Forget by hand", last.startswith("Done."),
      f"{secs:.1f} s, pairing {res.get('pairing')}")

failed = [c for c in checks if not c[1]]
rep = times.get("repeat", [])
summary = (f"{len(checks) - len(failed)}/{len(checks)} checks pass. First setup (app opened -> Done) "
           f"{times.get('first', 0):.1f} s; repeats {min(rep):.1f}-{max(rep):.1f} s." if rep else "")
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
back = times.get("back", [])
r.set("speed", (f"app opened -> Done: first {times.get('first', 0):.1f} s, repeats {min(rep):.1f}-{max(rep):.1f} s "
                f"({len(rep)} in a row)" + (f"; Done -> full firmware {min(back):.1f}-{max(back):.1f} s" if back else ""))
      if rep else None)
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
