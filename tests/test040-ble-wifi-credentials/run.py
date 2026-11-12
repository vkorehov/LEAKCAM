#!/usr/bin/env python3
"""test040 automated: the setup firmware's BLE provisioning driven from an Android phone over adb (the instrumented
BleOp of android/, one BLE operation per call), the stored credentials checked on the BL616 console (`pt_ble creds`:
length + CRC-32, never the passphrase). On the setup firmware's test bed (tests/firmware/bl616_setup_test:
firmware/bl616_setup's own sources, started by `lp_boot`, its switch to the full firmware off, so every store stays
here). Needs USB-C 1 in, the phone on adb, unlocked, Bluetooth on. The steps that need a hand on a cable (unplug,
power cut) are not in here: README Steps 1, 2 and 5 say which.

    tests/test040-ble-wifi-credentials/run.py [--flash]    --flash: bl616_setup_test built and flashed first
"""
import fcntl
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl, REPO   # noqa: E402
from board import BL_PORT, log, wifi_credentials   # noqa: E402
from phone import board_id, board_pin, creds, firmware, op, ready, want   # noqa: E402

SAVED, NOT_FOUND, WRONG_PSK, NO_API = 0x04, 0x10, 0x11, 0x19
PI = "pi@192.168.1.159"                         # the presence server (server/): the API the check ends at
CHECKED = [1, 2, 3, SAVED]                      # JOINING -> ADDRESS -> INTERNET (and the API) -> SAVED


def hexs(s):
    return s.encode().hex() if isinstance(s, str) else s.hex()


def states(res):
    return [s["state"] for s in res.get("states", [])]


def ble_on():
    """the setup firmware's start (`lp_boot`: RF, Wi-Fi, the product BLE), its switch to the full firmware off; True once
    BLE advertises (or the start had run)"""
    bl("lp_switch off", 2)
    return bool(re.search(r"ble: ready as|already run", bl("lp_boot", 20, r"ble: ready as|already run")))


def sess(what, writes, secs, **kw):
    """a paired session (as the app); one whose writes never left the phone (all -1: no link or no bond in time, the
    phone 8 m away in another room since 2026-11-03) never reached the board: retried once and noted, not counted
    against the board (store_cycles_capture.py, 2026-11-01: the one miss in 20 was the phone's)"""
    for attempt in (1, 2):
        res = op("session", address=addr, pair="accept", writes=writes, secs=secs, **kw)
        w = res.get("writes", [])
        if all(x["status"] == -1 for x in w):
            r.obstacle(f"{what}: no write left the phone (connect {res.get('connect_ms')} ms, pair "
                       f"{res.get('pair')!r}, bond {res.get('bond')}, error {res.get('error')!r})"
                       + (", retried" if attempt == 1 else ""))
            continue
        break
    if "commit" in writes and not res.get("states"):   # a commit without a STATUS read: what the phone saw
        log("session", what, "no STATUS:", {k: res.get(k) for k in ("connect_ms", "pair", "bond", "ccc", "error")},
            [x["status"] for x in res.get("writes", [])])
    return res


def bl_back(what, secs=40):
    """after a BL616 reset: wait for the console, the setup firmware started again. The board keeps no bond keys
    (CONFIG_BT_SETTINGS off, SECURITY.txt), so the phone forgets the board and pairs again (the app's 'Forget' case);
    probing the old bond first left a pairing dialog nobody pressed for 15 s (2026-10-28)"""
    t0 = time.time()
    time.sleep(2)
    while time.time() - t0 < secs and not os.path.exists(BL_PORT):
        time.sleep(0.5)
    time.sleep(2)
    check(f"BLE started again after {what}", ble_on())
    op("scan", secs=20)
    op("unpair", address=addr)
    op("session", address=addr, pair="accept", writes="", secs=1)


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
    # build first: `make flash` alone flashed a binary from before the mTLS change (2026-11-12: provision_tls "built
    # before mTLS?"), as the docstring always said
    bed = os.path.join(REPO, "tests/firmware/bl616_setup_test")
    subprocess.run(["make", "-C", bed], check=True)
    subprocess.run(["make", "-C", bed, "flash"], check=True)
    time.sleep(5)
if not os.path.exists(BL_PORT):
    r.obstacle("no BL616 console on USB (board or the VM's USB hub gone): reconnect, then run again")
    r.finish("not run: no BL616 console on USB.", False, switch_off=False)
    sys.exit(1)
if firmware() != "setup_test":
    r.obstacle(f"the BL616 runs {firmware()}: flash the setup test bed first (run.py --flash)")
    r.finish("not run: not the setup test bed.", False, switch_off=False)
    sys.exit(1)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)

# 1. advertising on USB power: time from the start (a reset, then lp_boot) to the phone's first sight of it
bl("reboot", 1)
time.sleep(2)
t1 = time.time()
while time.time() - t1 < 40 and not os.path.exists(BL_PORT):
    time.sleep(0.5)
time.sleep(1)
t0 = time.time()
check("BLE started (lp_boot)", ble_on())
res = op("scan", secs=20)
times["advertising"] = time.time() - t0
check("advertises on USB power", res.get("found"), f"{res.get('name')} rssi {res.get('rssi')} after {times['advertising']:.1f} s")
check("BLE name = the serial (USB serial number), no prefix", res.get("found") and res.get("name") == board_id(),
      f"{res.get('name')}")
if not res.get("found"):
    r.finish("the board was never seen advertising.", False)
    sys.exit(1)
addr = res["address"]
op("unpair", address=addr)
bl("pt_ble erase", 2)
r.sample("start")

# 2. encryption gate: pairing cancelled -> every write refused, nothing stored
res = op("session", address=addr, pair="cancel", writes=f"ssid:{hexs('t040-gate')},commit:02", secs=5)
w = res.get("writes", [])
# the link drops with the cancel (the board drops a failed pairing at once since 2026-11-04): no write, or -1
check("no write succeeds after a cancelled pairing", all(x["status"] != 0 for x in w) and (w or res.get("error")),
      f"{[x['status'] for x in w]} {res.get('error') or ''}")
check("nothing stored without pairing", creds() == (None, None))
# a stranger's phone is dropped (user 2026-11-04: no neighbour holds the board): one that never pairs after 10 s, one
# whose pairing fails (no PIN) at once; the owner's app connects right after
res = op("session", address=addr, pair="none", mtu=23, secs=20)
gone = (res.get("disconnected_ms") or 0) - (res.get("connect_ms") or 0)
check("a phone that never pairs: dropped after 10 s", res.get("connect_ms") is not None and 9000 <= gone <= 12000,
      f"{gone} ms after the connect")
op("unpair", address=addr)
res = op("session", address=addr, pair="accept", pin="", secs=20)
check("a phone without the PIN: not bonded, dropped", res.get("bond") != 12 and res.get("disconnected_ms") is not None
      and not res.get("writes"), f"{res.get('pair')}, dropped {res.get('disconnected_ms')} ms")
op("unpair", address=addr)

# 3. pairing and the characteristic limits (MTU 247 as the app)
t1 = time.time()
lim = [("ssid", "s" * 32, True), ("ssid", "s" * 33, False), ("psk", "p" * 7, False), ("psk", "p" * 64, False),
       ("psk", "p" * 8, True), ("psk", "p" * 63, True), ("commit", bytes([3]), False),
       ("commit", bytes([1, 1]), False)]       # no zero-length writes: the SDK's ATT layer refuses them (0x13)
res = sess("limits", ",".join(f"{c}:{hexs(v)}" for c, v, _ in lim), 3)
times["pair"] = time.time() - t1
check("paired (passkey entry with the PIN)", res.get("bond") == 12, f"{res.get('pair')}, MTU {res.get('mtu')}")
for (c, v, ok), x in zip(lim, res.get("writes", [])):
    check(f"{c} {len(v)} bytes {'accepted' if ok else 'refused'}", x["status"] == 0 if ok else x["status"] > 0,
          f"GATT {x['status']}")                # a refusal is the board's GATT error; -1 = never delivered
res = sess("commit without an SSID", "commit:02", 3)   # a new connection starts empty
check("commit without an SSID refused", res.get("writes") and res["writes"][0]["status"] > 0,
      f"GATT {res.get('writes', [{}])[0].get('status')}")
check("commit without an SSID stores nothing", creds() == (None, None))
res = sess("offset write", f"ssid:{hexs('o' * 30)}", 3, mtu=23)   # long write: offsets
check("offset (long) write refused", res.get("writes") and res["writes"][0]["status"] > 0,
      f"GATT {res.get('writes', [{}])[0].get('status')}")

# 4. store unchecked (0x02), read back exactly; 6. overwrite with shorter values
for s_, p_ in (("t040-" + "L" * 27, "P" * 63), ("t040-s", "pass1234")):
    t1 = time.time()
    res = sess(f"store {len(s_)}/{len(p_)}", f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:02", 10)
    times.setdefault("store", time.time() - t1)
    check(f"store unchecked {len(s_)}/{len(p_)} bytes: SAVED", states(res) == [SAVED], str(states(res)))
    check(f"stored exactly {len(s_)}/{len(p_)} bytes", creds() == want(s_, p_))
mark = want("t040-s", "pass1234")

# 5. persistence: a reset, a watchdog reset (the crash recorder's hang, its record cleared after)
bl("reboot", 1)
bl_back("a reset")
check("kept over a reset", creds() == mark)
bl("crash hang", 1)
time.sleep(16)                                  # watchdog recovery 12.9 s (test038)
bl_back("a watchdog reset")
check("kept over a watchdog reset", creds() == mark)
bl("crash clear", 2)

# 7. checked store: failures keep the old credentials, the right ones are stored after JOINING -> ADDRESS -> INTERNET
bad = psk[:-1] + ("x" if psk[-1] != "x" else "y")
for name, s_, p_, final in (("wrong passphrase", ssid, bad, (WRONG_PSK,)),   # 0x11 since the SDK firmware status is read (test106)
                            ("unknown SSID", "T040-NONE", psk, (NOT_FOUND,))):
    res = sess(f"check, {name}", f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:01", 90)
    st = states(res)
    check(f"check, {name}: {final[0]:#x}", st and st[-1] in final, str(st))
    check(f"check, {name}: old credentials kept", creds() == mark)
# the API down (the presence server stopped): the internet answers, the API does not -> NO_API, nothing stored
pin = board_pin()                               # from the server: before it is stopped
subprocess.run(["ssh", "-o", "BatchMode=yes", PI, "sudo systemctl stop leakcam-server"], check=True, timeout=30)
try:
    res = sess("check, the API down", f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01", 90, pin=pin)
finally:
    subprocess.run(["ssh", "-o", "BatchMode=yes", PI, "sudo systemctl start leakcam-server"], check=True, timeout=30)
st = states(res)
check(f"check, the API down: {NO_API:#x}", st[:3] == [1, 2, 3] and st[-1:] == [NO_API], str(st))
check("check, the API down: old credentials kept", creds() == mark)
res = op("session", address=addr, pair="accept", writes=f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01", drop_at="01", secs=10)
t0 = time.time()                                # the check runs on; the phone is gone: until it stored (or 60 s)
while time.time() - t0 < 60 and creds() != want(ssid, psk):
    time.sleep(2)
# the board finishes and stores: the phone reconnects and reads STATUS for its attempt (tests/test110, 2026-11-01)
check("check with the link dropped: the board finishes and stores", creds() == want(ssid, psk), str(states(res)))
res = sess("checked store", f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:01,commit:01", 90)
st = states(res)
check("checked store: JOINING -> ADDRESS -> INTERNET -> SAVED", st == CHECKED, str(st))
check("a second commit during the check starts no second check", st.count(1) == 1)
check("checked store: the real network stored", creds() == want(ssid, psk))
times["check"] = [(s["state"], s["ms"]) for s in res.get("states", [])]

# 9. repeats: 8 connect / store / disconnect cycles (the leak verdict's minimum), BL616 memory after each (sess():
#    a cycle whose writes never left the phone retried once)
CYCLES = 8
fails = 0
for i in range(CYCLES):
    s_, p_ = (f"t040-rep{i}", f"repeat-{i:04d}")
    res = sess(f"store cycle {i + 1}", f"ssid:{hexs(s_)},psk:{hexs(p_)},commit:02", 10)
    fails += states(res) != [SAVED] or creds() != want(s_, p_)
    r.sample(f"cycle {i + 1}")
check(f"{CYCLES} store cycles", fails == 0, f"{fails} failed")
res = sess("the real network at the end", f"ssid:{hexs(ssid)},psk:{hexs(psk)},commit:02", 10)
check("the real network stored again at the end", creds() == want(ssid, psk))
op("unpair", address=addr)

bad_ = [c for c in checks if not c[1]]
ck = times.get("check", [])
r.set("speed", f"advertising seen {times['advertising']:.1f} s after lp_boot; connect + pair {times.get('pair', 0):.1f} s; "
               f"store 0x02 {times.get('store', 0):.1f} s per session; check 0x01 "
               + " -> ".join(f"{s:#x} {ms / 1000:.1f} s" for s, ms in ck) + f" ({r.date}, run.py)")
r.set("boot", f"n/a: the K230 is not booted (BL616 only) ({r.date})")
r.set("voltage", f"n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) ({r.date})")
r.set("perday", f"n/a: on USB power only, once per installation ({r.date})")
r.finish(f"{len(checks) - len(bad_)} of {len(checks)} checks passed"
         + ("; failed: " + "; ".join(f"{n} ({d})" for n, _, d in bad_) if bad_ else ".")
         + " Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).",
         not bad_, switch_off=False)
