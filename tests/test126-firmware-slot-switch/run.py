"""test126: the slot switch between the full firmware (slot 0) and the setup firmware's test bed (slot 1), both ways.

Steps 1-6 of README.md: which firmware answers and which slot is active after the pair flash, then [rounds] round trips
of `slot switch` (full -> setup -> full), each timed from the command to the new firmware's console; the settings
both read (PIN state, the stored network's length and CRC: never the values), the crash record, the heap at start,
the partition table's ages (the FW entry's top byte is boot2's retry count).

    python3 run.py [rounds] [--stress]   (default 2, --stress 10; USB-C 1 in, the pair flashed: README Setup)
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tools/power-bringup"))
from board import BL_PORT, bl, xhci_reset   # noqa: E402

SWITCH_WAIT_S = 40


def ask(c, secs=3, until=None):
    return re.sub(r"\x1b\[[0-9;]*m", "", bl(c, secs, until))


def log(*a):
    print(time.strftime("%H:%M:%S", time.gmtime()), *a, flush=True)


def slot():
    """(active index, table age, FW entry age) from `slot`"""
    o = ask("slot", 3, r"slot: active[^\n]*\n")
    m = re.search(r"slot: active (\d) \(0x[0-9a-f]+\), age (\d+)", o)
    t = re.search(r"entryCnt \d+; age (\d+)", o)
    if not m or not t:
        return None
    return int(m.group(1)), int(t.group(1)), int(m.group(2))


def which():
    """'full' or 'setup' from the image `slot` names (leakcam-build <image> <version>), or None. Asked up to 5 times: the full firmware's Wi-Fi
    logs drown an answer now and then, and a runner that took None for "not full" switched it away (test128,
    2026-11-02): callers act only on a definite answer"""
    for _ in range(5):
        o = ask("slot", 3, r"slot: active[^\n]*\n")
        m, a = re.search(r"leakcam-build (full|setup) ", o), re.search(r"slot: active (\d)", o)
        # one answer names both: the image and the active slot must agree (full in 0, setup in 1), else asked again
        # (a garbled read once said setup in slot 0, 2026-11-07)
        if m and a and {"full": "0", "setup": "1"}[m.group(1)] == a.group(1):
            return m.group(1)
    return None


def settings(fw):
    """PIN state and the stored network's (len, crc) pairs: the CRCs compared, never printed"""
    for _ in range(3):                            # both firmwares are the product's now: `provision`; asked again
        o = ask("provision", 3, r"wifi_psk[^\n]*\n")   # when the first command after the port opened got no answer
        if "wifi_psk" in o:
            break
    pin = re.search(r"pairing PIN (\w+)", o)
    net = tuple(re.findall(r"(wifi_ssid|wifi_psk) len (\d+) crc ([0-9a-f]{8})", o))
    return (pin.group(1) if pin else None), net


def heap():
    m = re.search(r"mem: free (\d+)", ask("mem", 2, r"mem: free \d+"))
    return int(m.group(1)) if m else None


def crash():
    o = ask("crash", 2, r"crash: [^\n]*")
    m = re.search(r"crash: ([^\r\n]*)", o)
    return m.group(1) if m else "?"


def switch(want):
    """to the setup firmware `switch_to_setup lost` (it stays until switched back: a bare `slot switch` with USB power and
    a network stored hands straight back to the full firmware since test121/test127; the phone's wake is gone,
    2026-11-07), to the full one `slot switch`; seconds
    until the next firmware answers `slot`, or None"""
    t0 = time.time()
    bl("switch_to_setup lost" if want == "setup" else "slot switch", 1)
    gone, reset = False, False
    while time.time() - t0 < SWITCH_WAIT_S:
        if not os.path.exists(BL_PORT):
            gone = True
            if not reset and time.time() - t0 > 15:   # the VM's USB controller stopped (VMware, not the board)
                log("the BL616 port missing 15 s: the VM's USB controller reset (counted as a bench event)")
                resets.append(1)
                xhci_reset()
                reset = True
        elif gone and slot() is not None:
            return time.time() - t0
        time.sleep(0.2)
    return None


resets = []


def state(tag):
    fw = which()
    s = slot()
    pin, net = settings(fw)
    st = {"fw": fw, "slot": s, "pin": pin, "net": net, "heap": heap(), "crash": crash()}
    log(tag, f"firmware {fw}, slot {s[0] if s else '?'} (table age {s[1] if s else '?'}, FW entry age "
        f"0x{s[2]:08x})" if s else "slot ?", f"PIN {pin}, network stored {'yes' if net else 'no'}, heap {st['heap']},"
        f" crash: {st['crash']}")
    return st


def main():
    # regular runs: 2 round trips; the repeats are the stress run's (run_tests.py --stress)
    nums = [a for a in sys.argv[1:] if a.isdigit()]
    rounds = int(nums[0]) if nums else (10 if "--stress" in sys.argv else 2)
    fails = []
    first = state("start")
    if first["fw"] != "full" or not first["slot"] or first["slot"][0] != 0:
        fails.append("step 1: not the full firmware in slot 0 after the pair flash")
    times = {"full->setup": [], "setup->full": []}
    prev = first
    for r in range(1, rounds + 1):
        for want in ("setup", "full"):
            d = f"{prev['fw']}->{want}"
            secs = switch(want)
            if secs is None:
                fails.append(f"round {r} {d}: no console within {SWITCH_WAIT_S} s")
                log(f"round {r} {d}: NO CONSOLE")
                break
            st = state(f"round {r} {d} in {secs:.1f} s:")
            times.setdefault(d, []).append(secs)
            if st["fw"] != want:
                fails.append(f"round {r} {d}: {st['fw']} answered")
            if st["slot"] and prev["slot"] and st["slot"][1] != prev["slot"][1] + 1:
                fails.append(f"round {r} {d}: table age {prev['slot'][1]} -> {st['slot'][1]}")
            if st["pin"] != first["pin"] or st["net"] != first["net"]:
                fails.append(f"round {r} {d}: settings differ from the start")
            prev = st
        else:
            continue
        break
    for d, v in times.items():
        if v:
            log(f"{d}: {len(v)} switches, {min(v):.1f} / {sorted(v)[len(v) // 2]:.1f} / {max(v):.1f} s (min / median / max)")
    for f in fails:
        log("FAIL", f)
    if resets:
        log(f"VM USB controller resets: {len(resets)} (their switches' times include the reset)")
    log("PASS" if not fails else f"FAIL ({len(fails)})")


if __name__ == "__main__":
    main()
