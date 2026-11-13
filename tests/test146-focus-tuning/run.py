#!/usr/bin/env python3
"""test146 step 2: LEAKCAM Focus Adjustment's session on the board, through the server (DESIGN.md 5.3.17), the bench as
the app: Tune focus asked (POST /v1/devices/<id>/focus, camera 1), the board's check-in at once (`presence now`; in the
product it comes within 60 s while a camera is flagged), the live records read from GET /v1/focus/<id>/live for SECS s,
then a switch to camera 2 and back (POST .../cmd), each timed to the new camera's first picture (an IDR), then Stop:
the board's upload ends, SENT, 3 check-ins after. Each camera's pictures decoded with ffmpeg (2592x1936, the session's
parameter sets first, as the phone's one decoder keeps them). The BL616's lwIP (no TCP left: skill leakcam-test-metrics
5b) and the K230's page pool before and after.
    tests/test146-focus-tuning/run.py
"""
import json
import os
import re
import select
import statistics
import struct
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, log, openp   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

SECS = 6
SWITCH_MS = 400                          # the command's poll (at once: a long poll waits) + the next frame + the link
TEXT = 1 << 30
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def clean(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def post(path, body):
    req = urllib.request.Request(SERVER + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as f:
        return json.loads(f.read())


def nal_types(au, head=4096):
    return [(m.group(1)[0] >> 1) & 0x3f for m in re.finditer(rb"\x00\x00\x01(.)", au[:head], re.S)]


def idr(au):
    return any(t in (19, 20) for t in nal_types(au))


def bl_tcp():
    """the BL616's lwIP: its heap line and any TCP connection left (bl616_common/mem_stat.c)"""
    o = clean(bl("mem", 2))
    return " | ".join(re.findall(r"^lwip: [^\r\n]*", o, re.M))


klines = []                              # the K230's console, every line with its time (k230_console.log)
klock = threading.Lock()


def krecord(fd, path):
    """the K230's console read all the time (its log lines come when they come), kept in klines and the run's
    k230_console.log: a failed run's evidence (2026-11-13: the first board run left none)"""
    buf = b""
    with open(path, "w", buffering=1) as f:
        while True:
            if select.select([fd], [], [], 0.2)[0]:
                try:
                    buf += os.read(fd, 65536)
                except OSError:
                    return
                *lines, buf = buf.split(b"\n")
                for l in lines:
                    t = clean(l.decode(errors="replace")).rstrip()
                    if t:
                        with klock:
                            klines.append((time.time(), t))
                        f.write(f"{time.strftime('%H:%M:%S')} {t}\n")


def k230(fd, c, secs):
    """a command on the K230's console: the lines of the next secs"""
    t = time.time()
    os.write(fd, c.encode() + b"\r\n")
    time.sleep(secs)
    with klock:
        return "\n".join(l for at, l in klines if at >= t)


def pages_free(fd):
    m = re.findall(r"free pages is ([0-9a-f]+)", k230(fd, "list_page", 1.5))
    return int(m[-1], 16) if m else None


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish("not run: the board is not in the full firmware", False, switch_off=False)
    sys.exit(1)
kfd = openp(K230_PORT)
threading.Thread(target=krecord, args=(kfd, os.path.join(r.dir, "k230_console.log")), daemon=True).start()
t = time.time()                          # a capture of the board's own first (a leak check's pictures, a video)
while "one running" in clean(bl("capture", 2)) and time.time() - t < 120:
    time.sleep(2)
free0 = pages_free(kfd)
records = []                             # (time, cam of the last CAM line, text?, payload)
done_reading = threading.Event()


def reader():
    """GET /v1/focus/<dev>/live: every record with its arrival time"""
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/focus/{dev}/live", timeout=120) as f:
            cam = -1
            while True:
                h = f.read(12)
                if len(h) < 12:
                    break
                l, _ = struct.unpack(">IQ", h)
                p = f.read(l & ~TEXT)
                if l & TEXT:
                    t = p.decode(errors="replace")
                    if t.startswith("CAM,"):
                        cam = int(t.split(",")[1])
                    records.append((time.time(), cam, True, t))
                else:
                    records.append((time.time(), cam, False, p))
    except Exception as e:                # noqa: BLE001 - the end of the stream, or none
        log("live", f"ended: {e}")
    done_reading.set()


a = post(f"/v1/devices/{dev}/focus", {"cam": 0})
check("Tune focus asked (the server's answer)", "within_s" in a, json.dumps(a))
threading.Thread(target=reader, daemon=True).start()
t_ask = time.time()
bl("presence now", 1)                    # the check-in now (in the product: within 60 s while flagged)
while time.time() - t_ask < 60 and not any(not x[2] for x in records):
    time.sleep(0.1)
first = next((x[0] - t_ask for x in records if not x[2]), None)
check("the first picture within 30 s of the check-in", first is not None and first < 30,
      f"{first:.1f} s" if first is not None else "none")
switches = []
for want in (1, 0):
    time.sleep(SECS)
    n0, t_cmd = len(records), time.time()
    post(f"/v1/focus/{dev}/cmd", {"cam": want})
    while time.time() - t_cmd < 5:
        new = [x for x in records[n0:] if not x[2] and x[1] == want]
        if new:
            switches.append((want, (new[0][0] - t_cmd) * 1000, idr(new[0][3])))
            break
        time.sleep(0.02)
    log("SWITCH", str(switches[-1]) if switches and switches[-1][0] == want else f"camera {want + 1}: none")
time.sleep(SECS)
post(f"/v1/focus/{dev}/cmd", {"stop": True})
t_stop = time.time()
done_reading.wait(20)
check("Stop ends the board's stream within 10 s", done_reading.is_set() and time.time() - t_stop < 10,
      f"{time.time() - t_stop:.1f} s")
for want, ms, is_idr in switches:
    check(f"switch to camera {want + 1}: its first picture within {SWITCH_MS} ms, an IDR", ms <= SWITCH_MS and is_idr,
          f"{ms:.0f} ms, IDR {is_idr}")
check("both switches seen", len(switches) == 2, str(switches))
records = list(records)                  # as they are now: nothing counted against a stream that goes on
pics = [x for x in records if not x[2]]
params = next((x[3] for x in pics if 32 in nal_types(x[3])), b"")
rows = []
for cam in (0, 1):
    fr = [x for x in pics if x[1] == cam]
    path = os.path.join(r.dir, f"cam{cam}.h265")
    with open(path, "wb") as f:
        f.write(params)
        for x in fr:
            f.write(x[3])
    dec = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_entries",
                          "stream=width,height,nb_read_frames", "-of", "csv=p=0", path], capture_output=True,
                         text=True, timeout=120)
    err = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "null", "-"], capture_output=True, text=True,
                         timeout=120).stderr.strip()
    gaps = [b[0] - a[0] for a, b in zip(fr, fr[1:]) if b[0] - a[0] < 1]
    fps = 1 / statistics.median(gaps) if gaps else 0
    scores = [x for x in records if x[2] and x[3].startswith(f"FOC,{cam},")]
    rows.append(f"cam{cam}: {len(fr)} pictures, {fps:.1f} fps at the app, {len(scores)} scores")
    log("VIDEO", rows[-1])
    check(f"camera {cam + 1}: every picture decodes, 2592x1936", fr and not err and dec.stdout.startswith("2592,1936"),
          f"{dec.stdout.strip()} {err[:150]}")
    check(f"camera {cam + 1}: ~15 fps at the app while shown", fps >= 12, f"{fps:.1f} fps")
    secs = sum(gaps)                     # the time this camera was shown
    check(f"camera {cam + 1}: a whole-frame score once a second", abs(len(scores) - secs) <= 2,
          f"{len(scores)} scores in {secs:.1f} s")
t = time.time()
while "one running" in clean(bl("capture", 2)) and time.time() - t < 30:
    time.sleep(1)
cap = clean(bl("capture", 2))
check("SENT: the BL616's capture over, the bridge off", "one running" not in cap and "bridge off" in cap,
      cap.strip().splitlines()[-2] if cap.strip() else "")
states = []
for i in range(3):
    o = clean(bl("presence now", 25, r'check of "[^"]*": state[^\n]*\n'))
    m = re.search(r'check of "[^"]*": state (0x\w+)', o)
    states.append(m.group(1) if m else "none")
check("3 check-ins after the session: all reach the server (0x03)", states == ["0x03"] * 3, " ".join(states))
lwip = bl_tcp()
check("the BL616 holds no TCP connection after (memory leakcam-memory-metrics-leaks)", "lwip: tcp" not in lwip, lwip)
free1 = pages_free(kfd)
check("the K230's page pool back after the session (within 64 pages)", free0 and free1 and free1 >= free0 - 64,
      f"{free0} -> {free1} pages free")
r.set("Speed / rate against the top", "; ".join(rows) + "; switches " +
      ", ".join(f"{ms:.0f} ms" for _, ms, _ in switches))
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
