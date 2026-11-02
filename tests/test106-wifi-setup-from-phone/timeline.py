#!/usr/bin/env python3
"""test106 timeline: one setup through the app (BleOp op app) with every wait the user sees laid against what the
board does at that moment - the app's screen changes (phone clock, moved to the host clock by an adb offset) and the
BL616 console lines (host clock as they arrive). Lines holding the passphrase are withheld.

    tests/test106-wifi-setup-from-phone/timeline.py [good | wrongpsk | gone] [--all] [--repeat N]
        --all: every console line; --repeat N: N setups in a row (one boot), one row of stage times each
"""
import os
import re
import select
import subprocess
import sys
import threading
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import bl   # noqa: E402
from board import BL_PORT, openp, wifi_credentials   # noqa: E402
from phone import op, ready   # noqa: E402

# console lines that mark a step (the rest only with --all)
KEY = re.compile(r"(?i)ble:|connected|disconnect|pairing|smp|security|encrypt|wifi_ssid|wifi_psk|commit|WCHK|"
                 r"Trying to associate|Associated|EAPOL|4-Way|CTRL-EVENT|DHCP|IP|got ip|dns|http|saved|scan")


def phone_offset():
    """phone clock - host clock, s (the adb round trip halved)"""
    best = None
    for _ in range(5):
        a = time.time()
        p = float(subprocess.run(["adb", "shell", "date", "+%s.%N"], capture_output=True, text=True).stdout.strip())
        b = time.time()
        if best is None or b - a < best[0]:
            best = (b - a, p - (a + b) / 2)
    return best[1]


# the app's screens that end each stage the user waits through (MainActivity strings, as logged)
MARKS = (("found", "step 3"), ("send", "Connecting"), ("prompt", "asks to pair"), ("paired", "Paired."),
         ("check", "joining"), ("joined", "Password accepted"), ("address", "Connected to the router"),
         ("end", "step 5|error"))


def stages(ev, t0):
    """seconds from opening the app to each mark"""
    out = {}
    for name, pat in MARKS:
        hit = [ts for ts, who, text in ev if who == "APP  " and re.search(pat, text)]
        out[name] = round(hit[0] - t0, 2) if hit else None
    return out


def main():
    case = next((a for a in sys.argv[1:] if not a.startswith("--")), "good")
    n = int(sys.argv[sys.argv.index("--repeat") + 1]) if "--repeat" in sys.argv else 1
    print("phone:", ready([]) or "ready")
    if "pt_ble: on" not in bl("pt_ble", 2):
        bl("pt_ble on", 5, r"ble: ready as")
    rows = [run(case, n == 1) for _ in range(n)]
    if n > 1:
        print("open->:  " + "  ".join(f"{name:>7}" for name, _ in MARKS))
        for st in rows:
            print("         " + "  ".join(f"{st[name]:7.2f}" if st[name] is not None else "      -" for name, _ in MARKS))


def run(case, verbose):
    ssid, psk = wifi_credentials()
    s, p = {"good": (ssid, psk), "wrongpsk": (ssid, psk + "x"), "gone": ("t106-not-here", "password1")}[case]
    off = phone_offset()
    lines, stop = [], threading.Event()

    def reader():
        fd = openp(BL_PORT)
        buf = b""
        while not stop.is_set():
            if select.select([fd], [], [], 0.2)[0]:
                buf += os.read(fd, 4096)
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace")).strip()
                    if line:
                        lines.append((time.time(), line))
        os.close(fd)

    t = threading.Thread(target=reader, daemon=True)
    subprocess.run(["adb", "logcat", "-c"])
    t.start()
    t_call = time.time()
    res = op("app", ssid=s.encode().hex(), psk=p.encode().hex(), secs=150)
    time.sleep(0.5)
    stop.set()
    t.join()
    t0 = res.get("t0_epoch_ms", t_call * 1000) / 1000 - off      # the op's start on the host clock
    # the app logs each screen change itself (tag LeakcamSetup): exact, where op app's screen polling lags ~0.5 s
    log = subprocess.run(["adb", "logcat", "-d", "-v", "epoch", "-s", "LeakcamSetup:I"], capture_output=True,
                         text=True).stdout
    ev = [(float(m.group(1)) - off, "APP  ", m.group(2)[:110])
          for m in re.finditer(r"^\s*(\d+\.\d+)\s.*?LeakcamSetup:\s(.*)$", log, re.M)]
    ev += [(t0 + h["ms"] / 1000, "op   ", h["help"].split("\n")[0][:60]) for h in res.get("help", [])] if not ev else []
    for k in ("ms_found", "ms_anyway", "ms_end"):
        if k in res:
            ev.append((t0 + res[k] / 1000, "op   ", k))
    hexkey, show_all = p.encode().hex(), "--all" in sys.argv
    for ts, line in lines:
        if p in line or hexkey in line.lower():
            line = "<line with the passphrase withheld>"
        if show_all or KEY.search(line):
            ev.append((ts, "BL616", line[:110]))
    st = stages(ev, t0)
    if not verbose:
        return st
    print(f"case {case}: op call -> t0 {t0 - t_call:+.2f} s (instrumentation start), clock offset {off:+.3f} s")
    prev = None
    for ts, who, text in sorted(ev):
        gap = f"+{ts - prev:5.2f}" if prev is not None else "      "
        print(f"{ts - t0:7.2f} {gap}  {who}  {text}")
        prev = ts
    print({k: v for k, v in res.items() if k not in ("help",)})
    print("stages (s from opening the app):", st)
    return st


if __name__ == "__main__":
    main()
