#!/usr/bin/env python3
"""A board's focus session (DESIGN.md 5.3.17; k230_app/focus.c), mocked from recorded frames, against a server: the
stream POST /v1/focus/<id>/stream (records [u32 BE length | 1<<30 text][u64 BE us][payload]: per frame the shown
camera's access unit, CAM,<cam> before a camera's first, its score FOC,<cam>,<seq>,<score> once a second) and the commands by the long poll GET /v1/focus/<id>/cmd
({"cam":n}: the other camera from its next IDR, as the K230 sends it; {"done":s}: FOCUS,OK,<cam>,<s>; {"stop":true}:
the end). The frames: tests/test146-focus-tuning/cam<N>.fv ([u32 BE length][u8 IDR][access unit] each, made from the
board's recording of 2026-11-13; the first carries the parameter sets).
    mock_board_focus.py <server> <device> <cam0.fv> <cam1.fv> [max s]
Prints one line per event (start, each command with its time, the end) for the test runner.
"""
import http.client
import json
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request

server, dev, f0, f1 = sys.argv[1:5]
max_s = float(sys.argv[5]) if len(sys.argv) > 5 else 120
FPS = 15
TEXT = 1 << 30


def frames(path):
    out, raw, i = [], open(path, "rb").read(), 0
    while i + 5 <= len(raw):
        n, idr = struct.unpack(">IB", raw[i:i + 5])
        out.append((bool(idr), raw[i + 5:i + 5 + n]))
        i += 5 + n
    return out


streams = [frames(f0), frames(f1)]
def param_sets(au):
    """the VPS, SPS, PPS (NAL 32-34) of an access unit, each with a 4-byte start code"""
    parts = [p.rstrip(b"\0") for p in au.split(b"\0\0\1")[1:]]
    return b"".join(b"\0\0\0\1" + p for p in parts if p and 32 <= (p[0] >> 1) & 0x3f <= 34)


params = param_sets(streams[0][0][1])              # the K230 sends them once, with the session's first frame
state = {"cam": None, "switch": None, "stop": False, "done": None}
lock = threading.Lock()
t0 = time.time()


def log(*a):
    print(f"{time.time() - t0:7.3f}", *a, flush=True)


def poller():
    while True:
        with lock:
            if state["stop"]:
                return
        try:
            with urllib.request.urlopen(f"{server}/v1/focus/{dev}/cmd", timeout=10) as r:
                c = json.loads(r.read() or b"{}")
        except Exception as e:                     # noqa: BLE001 - the server gone: the session ends
            log("poll failed", e)
            c = {"stop": True}
        with lock:
            if "cam" in c:
                state["switch"] = (int(c["cam"]), time.time())
                log("cmd cam", c["cam"])
            if "done" in c:
                state["done"] = float(c["done"])
                log("cmd done", c["done"])
            if c.get("stop"):
                state["stop"] = True
                log("cmd stop")


u = urllib.parse.urlparse(server)
conn = http.client.HTTPConnection(u.hostname, u.port, timeout=30)
conn.putrequest("POST", f"/v1/focus/{dev}/stream")
conn.putheader("Content-Type", "application/x-leakcam-focus")
conn.putheader("Transfer-Encoding", "chunked")
conn.endheaders()


def send(data, us, text=False):
    rec = struct.pack(">IQ", len(data) | (TEXT if text else 0), us) + data
    conn.send(b"%x\r\n" % len(rec) + rec + b"\r\n")


threading.Thread(target=poller, daemon=True).start()
log("stream started")
at, seq, cam, first = [0, 0], 0, None, True
start = time.time()
while time.time() - start < max_s:
    with lock:
        if state["stop"]:
            break
        sw, state["switch"] = state["switch"], None
        done, state["done"] = state["done"], None
    if sw is not None and sw[0] != cam:
        cam = sw[0]
        s = streams[cam]                            # the board's IDR at the switch: this camera from its next one
        at[cam] = next(((at[cam] + k) % len(s) for k in range(len(s)) if s[(at[cam] + k) % len(s)][0]), 0)
        send(f"CAM,{cam}".encode(), int(seq * 1e6 / FPS), True)
        log(f"showing camera {cam} ({(time.time() - sw[1]) * 1000:.0f} ms after the command)")
    if cam is None:
        time.sleep(0.02)                            # the first poll brings the camera to show
        continue
    us = int(seq * 1e6 / FPS)
    if done is not None:
        send(f"FOCUS,OK,{cam},{done:.3f}".encode(), us, True)
    s = streams[cam]
    idr, au = s[at[cam]]
    score = lambda c: 1.0 + 0.3 * c + 0.01 * (seq % 7)   # a steady scene
    if seq % FPS == FPS - 1:
        send(f"FOC,{cam},{seq},{score(cam):.3f}".encode(), us, True)
    if first:
        send(params, us)                            # the parameter sets before the session's first frame
        first = False
    send(au, us)
    at[cam] = (at[cam] + 1) % len(s)
    seq += 1
    wait = start + seq / FPS - time.time()
    if wait > 0:
        time.sleep(wait)
conn.send(b"0\r\n\r\n")
r = conn.getresponse()
log("stream ended", r.status, r.read().decode().strip(), f"{seq} frames")
