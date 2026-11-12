#!/usr/bin/env python3
"""test143: Take photo -> two real pictures at the server, on USB power (no hand, no phone). Each round asks the server
for a photo (as the app's Take photo), triggers a check-in (`presence now`), waits for the BL616's `capture: SENT`,
then fetches both pictures and checks them (JPEG, size, light, X-Taken, EXIF time).
    tests/test143-photo-upload/run.py [rounds] [--cold]     default 3 rounds; --cold: the K230 switched off before
                                                            each round, the photo starts its session
"""
import io
import json
import os
import re
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

import numpy as np   # noqa: E402
from PIL import Image   # noqa: E402

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 3
COLD = "--cold" in sys.argv          # the K230 off first: the photo's own session (boot + the job's first check)
SENT_S = 60
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def clean(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def post(path, body):
    req = urllib.request.Request(SERVER + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as f:
        return json.loads(f.read())


def fetch(mid):
    """(bytes, X-Taken ms) or (None, status)"""
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/media/{mid}", timeout=10) as f:
            return f.read(), int(f.headers.get("X-Taken") or 0)
    except urllib.error.HTTPError as e:
        return None, e.code


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


r = Run(__file__)
dev = board_id()
fw = firmware()
if fw != "full":
    r.finish(f"not run: the board runs {fw or 'nothing'}, not the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
t = time.time()
while "one running" in bl("capture", 2) and time.time() - t < 130:   # a capture from before the run: its SENT first
    time.sleep(2)

mem0, asks, heaps = None, [], []
for rnd in range(1, ROUNDS + 1):
    if rnd == 2:
        # the baseline after round 1: the Wi-Fi bridge's first start allocates lwIP and NetHub for good (~11 KB, kept
        # across sessions: wifi_link.c), not a leak (2026-11-12 --cold: 156020 -> 145156 B, then flat)
        mem0 = bl_free()
    if COLD:
        bl("k230 off", 2)
        t = time.time()
        while "k230: off" not in clean(bl("k230", 1.5)) and time.time() - t < 10:
            pass
    before = clean(bl("capture", 2))
    prev = (re.search(r"last \w+ ([^\s,]+)", before) or [None, None])[1]    # the capture before this round's
    post(f"/v1/devices/{dev}/capture", {"kind": "photo"})
    t0 = time.time()
    bl("presence now", 1)
    base, sent = None, None
    while time.time() - t0 < SENT_S:
        r.console += cmd(r.fd, None, 1)                 # the K230's media steps as they come
        s = clean(bl("capture", 1.5))
        m = re.search(r"last photo ([^\s,]+)", s)
        if m and m.group(1) == prev:
            m = None                                  # not handed over yet: the last one is the earlier capture's
        if m and (base is None):
            base = m.group(1)
        if m and "one running" not in s:
            sent = time.time() - t0
            break
    t_end = time.time() + 3
    while time.time() < t_end and not re.search(r"media: done", r.console.split(f"media: photo {base}")[-1] if base else ""):
        r.console += cmd(r.fd, None, 1)
    check(f"round {rnd}: SENT within {SENT_S} s of the check-in", sent is not None,
          f"{sent:.1f} s, {base}" if sent else f"none ({base})")
    done = re.findall(r"media: done, (\d+) uploaded, (\d+) failed", r.console.split(f"media: photo {base}")[-1]
                      if base else "")
    check(f"round {rnd}: the K230 uploaded 2, failed 0", done and done[0] == ("2", "0"),
          f"{done[0][0]} uploaded, {done[0][1]} failed" if done else "no media: done line")
    if sent:
        asks.append(sent)
    if not base:
        continue
    takens = []
    for n, cam in ((1, "J4"), (2, "J5")):
        data, taken = fetch(f"{base}-p{n}")
        if data is None:
            check(f"round {rnd}: p{n} ({cam}) at the server", False, f"HTTP {taken}")
            continue
        try:
            im = Image.open(io.BytesIO(data))
            im.load()
            when = im.getexif().get_ifd(0x8769).get(0x9003)
            a = np.asarray(im.convert("L"), dtype=np.float32)
            ok = im.size == (2592, 1936) and a.mean() > 0   # native, cropped to 16-row blocks (vicap_cap.c)
            detail = f"{len(data)} B, {im.size[0]}x{im.size[1]}, mean {a.mean():.0f}, EXIF {when}"
        except Exception as e:                           # noqa: BLE001 - any decode failure fails the check
            ok, when, detail = False, None, f"does not decode: {e}"
        check(f"round {rnd}: p{n} ({cam}) a lit native 2592x1936 JPEG", ok, detail)
        exif_s = time.mktime(time.strptime(when, "%Y:%m:%d %H:%M:%S")) - time.timezone if when else 0
        check(f"round {rnd}: p{n} taken now (X-Taken and EXIF within 120 s, equal within 1 s)",
              taken and abs(taken / 1000 - time.time()) < 120 and abs(exif_s - taken / 1000) < 1.5,
              f"X-Taken {taken}, EXIF {when} UTC")
        takens.append(taken)
        if rnd == 1:
            with open(os.path.join(r.dir, f"p{n}_{cam}.jpg"), "wb") as f:
                f.write(data)
    if len(takens) == 2:
        check(f"round {rnd}: both cameras in the same shot (X-Taken within 1 s)", abs(takens[0] - takens[1]) < 1000,
              f"{takens[1] - takens[0]} ms apart")
# the board's own check-ins after the photos: the bridge must not outlive the capture (2026-11-12: with it up, 2 of 7
# check-ins failed and 3 in a row send the board to the setup firmware)
states = []
for i in range(5):
    o = clean(bl("presence now", 25, r'check of "[^"]*": state[^\n]*\n'))
    m = re.search(r'check of "[^"]*": state (0x\w+)', o)
    states.append(m.group(1) if m else "none")
check("5 check-ins after the photos: all reach the server (0x03)", states == ["0x03"] * 5, " ".join(states))
mem1 = bl_free()
r.clog_write("K230 console during the rounds", r.console)

steps = re.findall(r"media: (Wi-Fi bridge \w+|round 1 at \d+ ms: cameras 0x\d in \d+ ms, JPEGs at \d+ ms|"
                   r"p\d \d+ B: upload\w* \w* ?-?\d+ at \d+ ms|done, [^:]+): t \d+ ms, heap \+(-?\d+) B", r.console)
heaps = [int(h) for s, h in steps if s.startswith("done")]
log("K230 media steps:", "; ".join(s for s, _ in steps[-6:]))
check(f"{ROUNDS} rounds: the K230 program's heap does not grow", len(heaps) >= 2 and heaps[-1] <= heaps[0],
      f"heap + {heaps} B")
check(f"rounds 2-{ROUNDS}: the BL616's free heap does not shrink", mem0 and mem1 and mem1[0] >= mem0[0] - 512,
      f"{mem0} -> {mem1}")
if asks:
    r.set("Photo: ask", f"{min(asks):.1f}-{max(asks):.1f} s check-in -> SENT ({len(asks)} rounds), both pictures")
if steps:
    r.set("K230 steps", "; ".join(s for s, _ in steps[-6:]))
r.set("Memory BL616", f"BL616 free {mem0} -> {mem1} (free, min) over {ROUNDS} rounds; K230 heap after each {heaps} B")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
