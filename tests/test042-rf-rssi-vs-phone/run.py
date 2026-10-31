#!/usr/bin/env python3
"""test042: the board's RF against the bench phone, side by side (no RF instruments on the bench). One call per board
orientation; results appended to rssi.log in this folder.

    tests/test042-rf-rssi-vs-phone/run.py <label> [scans]      e.g. "flat, antenna north, phone beside"

Wi-Fi receive: the BL616 (`pt_wifi scan`) and the phone (`cmd wifi list-scan-results`) scan the access points around,
`scans` times each (default 10); per BSSID (channel) the median RSSI of each and board - phone.
BLE transmit: the phone's RSSI of the board's advertising (BleOp op scan), `scans` times, median.
Needs USB-C 1 in (BLE advertises only on USB power), the K230 off, the phone on adb and unlocked.
"""
import os
import re
import statistics
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-standard"))
from harness import bl   # noqa: E402  (puts tools/power-bringup on the path)
from phone import adb, op   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
label = sys.argv[1] if len(sys.argv) > 1 else "unlabelled"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10
OUT = os.path.join(HERE, "rssi.log")


def board_scan():
    out = re.sub(r"\x1b\[[0-9;]*m", "", bl("pt_wifi scan", 15, r"pt_wifi scan: \d+ APs[^\n]*\n"))
    return {m.group(4): (m.group(1).strip(), int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r"^\s+(.*?)\s+ch\s+(\d+)\s+(-?\d+) dBm\s+\S+\s+([0-9a-f:]{17})", out, re.M)}


def phone_scan():
    adb("shell", "cmd", "wifi", "start-scan", secs=30)
    time.sleep(6)
    out = adb("shell", "cmd", "wifi", "list-scan-results", secs=30)
    got = {}
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f:]{17})\s+(\d+)\s+(-?\d+)\s+\S+\s+(.*?)\s+\[", line)
        if m and 2400 < int(m.group(2)) < 2500:
            ch = (int(m.group(2)) - 2407) // 5
            got[m.group(1)] = (m.group(4).strip(), ch, int(m.group(3)))
    return got


def log(line):
    print(line, flush=True)
    with open(OUT, "a") as f:
        f.write(line + "\n")


if "pt_wifi" not in bl("help", 2):
    sys.exit("the BL616 test firmware has no pt_wifi")
if not adb("devices").strip().splitlines()[1:]:
    sys.exit("no phone on adb")
bl("pt_off", 2)
board, phone = {}, {}
for i in range(N):
    for b, (name, ch, rssi) in board_scan().items():
        board.setdefault(b, (name, ch, []))[2].append(rssi)
    for b, (name, ch, rssi) in phone_scan().items():
        phone.setdefault(b, (name, ch, []))[2].append(rssi)
log(f"\n## {time.strftime('%Y-%m-%d %H:%M:%S')} UTC{time.strftime('%z')} - {label} ({N} scans each)")
log("Wi-Fi receive, per access point (median dBm; seen = scans that saw it):")
log(f"  {'ch':>2}  {'board':>5} {'seen':>4}  {'phone':>5} {'seen':>4}  {'diff':>5}  bssid")
diffs = []
for b in sorted(set(board) | set(phone), key=lambda x: (board.get(x, phone.get(x))[1], x)):
    ch = (board.get(b) or phone.get(b))[1]
    bm = statistics.median(board[b][2]) if b in board else None
    pm = statistics.median(phone[b][2]) if b in phone else None
    d = bm - pm if bm is not None and pm is not None else None
    if d is not None and len(board[b][2]) >= N // 2 and len(phone[b][2]) >= N // 2:
        diffs.append((ch, d))
    f = lambda v: f"{v:5.0f}" if v is not None else "    -"
    log(f"  {ch:2d}  {f(bm)} {len(board[b][2]) if b in board else 0:4d}  {f(pm)} {len(phone[b][2]) if b in phone else 0:4d}  "
        f"{f(d)}  {b}")
if diffs:
    by = {}
    for ch, d in diffs:
        by.setdefault(ch, []).append(d)
    log("  board - phone, median per channel (APs seen in at least half the scans by both): " +
        ", ".join(f"ch {c} {statistics.median(v):+.0f} dB ({len(v)} AP)" for c, v in sorted(by.items())) +
        f"; all {statistics.median([d for _, d in diffs]):+.0f} dB")
ble = []
bl("pt_ble on", 5, r"advertising as|already on")
for i in range(N):
    r = op("scan", secs=10)
    if r.get("found"):
        ble.append(int(r["rssi"]))
log(f"BLE transmit: the phone heard the board in {len(ble)} of {N} scans" +
    (f", RSSI median {statistics.median(ble):.0f} dBm (min {min(ble)}, max {max(ble)})" if ble else ""))
