#!/usr/bin/env python3
"""test106 automated: the LEAKCAM Setup app itself driven on the bench phone (BleOp op app: Find, the network, Pair,
check and save, the pairing dialogs) against the BL616 product pair (firmware/bl616_full in slot 0, bl616_setup in
slot 1: all setup in the setup firmware, every stored network switches the board back to the full firmware) or the
old test firmware; what the board stored checked on its console (`provision` / `pt_ble creds`: length + CRC-32, never
the passphrase). Needs USB-C 1 in, the phone on adb (Wi-Fi), unlocked, Bluetooth on.
README Steps 1, 3, 4, 5, 6 and 8; step 2 (wakes on battery, the K230 session) needs the rewritten product firmware,
step 7 a hand on the cable (test040 hand_steps.py usb-gate covers the board side).
--battery (product pair): REPEATS setups in a row with USB-C 1 out (hands: out at the start, in at the end): each
from the full firmware (`switch_to_setup lost`), the setup firmware found asleep by the phone (no console there),
Done, back in the full firmware with the network stored (bugs/bug-006: tests on USB never began its sleep).
    tests/test106-wifi-setup-from-phone/run.py [--battery]
"""
import fcntl
import os
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, bl_back, log, wifi_credentials   # noqa: E402
from phone import board_id, creds, firmware, hand, only_boards, op, product, ready, seen, to_firmware, want   # noqa: E402

STRESS = "--stress" in sys.argv
# the repeats are the stress run's (run_tests.py --stress): 8 = the leak verdict's minimum (mem_leak.py); a regular
# run (the essential suite, tests/README.md) does one
REPEATS = 8 if STRESS else 1
BATTERY = "--battery" in sys.argv

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
    # Setup lists new devices only (DESIGN.md 5.3.4, user 2026-11-07): the phone starts each setup with no boards (the
    # last setup registered it when the app asked for the PIN)
    only_boards()
    res = op("app", ssid=ssid.encode().hex(), psk=psk.encode().hex(), anyway="1" if anyway else "", secs=45)
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
if BATTERY:
    # the setups on the cell: the full firmware has a console on battery, the sleeping setup firmware none
    dev = board_id()
    if not PRODUCT or firmware() != "full" or creds() != want(ssid, psk):
        r.obstacle("--battery needs the product pair in the full firmware with the real network stored")
        r.finish("not run: not the full firmware with the network.", False, switch_off=False)
        sys.exit(1)
    check("USB-C 1 out (hand)", hand("unplug USB-C 1 (the cell stays in)", "unplugged") is not None)
    found = []
    done = 0
    for i in range(REPEATS):
        if firmware() == "full":
            bl("switch_to_setup lost", 1)
        t = seen(dev, 60)
        found.append(t)
        if t is None:
            check(f"battery setup {i + 1}: found", False, "not within 60 s")
            break
        last, secs, res = setup(ssid, psk)
        done += last.startswith("Done.")
        times.setdefault("repeat", []).append(secs)
    check(f"{REPEATS} setups on battery: every one found and Done", done == REPEATS,
          f"{done}/{REPEATS}, found after " + ", ".join(f"{t:.0f}" if t is not None else "-" for t in found) + " s")
    check("on battery: the network stored", creds() == want(ssid, psk))
    check("USB-C 1 back in (hand)", hand("plug USB-C 1 back in", "plugged") is not None)
    failed = [c for c in checks if not c[1]]
    rep_ = times.get("repeat", [])
    r.set("speed", f"on battery: found {min(t for t in found if t is not None):.0f}-"
                   f"{max(t for t in found if t is not None):.0f} s after the switch, app opened -> Done "
                   f"{min(rep_):.1f}-{max(rep_):.1f} s ({len(rep_)} in a row)" if rep_ else None)
    r.finish(f"{len(checks) - len(failed)}/{len(checks)} checks pass (on battery)."
             + (" Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "." if failed else ""),
             not failed, switch_off=False)
    sys.exit(1 if failed else 0)

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
bl_back(60)                                     # the shell answering again (the watchdog's recovery 12.9 s, test038)
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
