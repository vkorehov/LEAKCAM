#!/usr/bin/env python3
"""test147: Take video -> two native H.265 videos at the server, live while they are made, on USB power (no hand, no
phone). Each round asks the server for a video (as the app's Take video), triggers a check-in (`presence now`); while
the board records, the server's live HLS of camera 1 is polled; after the BL616's `capture: SENT` both MP4s are fetched
and checked with ffprobe (HEVC, 2592x1936, 15 fps, 15 s, every frame decodes, the creation time now).
    tests/test147-video-h265/run.py [rounds]       default 1
"""
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, cmd, log, openp   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 1
SECS, FPS = 15, 15
SENT_S = 90                          # the check-in, the bridge, 15 s of video, the upload's tail, the MP4
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
    try:
        with urllib.request.urlopen(f"{SERVER}/v1/media/{mid}", timeout=20) as f:
            return f.read(), int(f.headers.get("X-Taken") or 0)
    except urllib.error.HTTPError as e:
        return None, e.code


def probe(path):
    """ffprobe's stream and format facts, and whether every frame decodes"""
    out = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_entries",
                          "stream=codec_name,codec_tag_string,width,height,avg_frame_rate,nb_read_frames:"
                          "format=duration:format_tags=creation_time", "-of", "json", path],
                         capture_output=True, text=True, timeout=120)
    info = json.loads(out.stdout or "{}")
    dec = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "null", "-"], capture_output=True, text=True,
                         timeout=120)
    return info, dec.stderr.strip()


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish("not run: the board is not in the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
t = time.time()
while "one running" in bl("capture", 2) and time.time() - t < 130:
    time.sleep(2)

for rnd in range(1, ROUNDS + 1):
    prev = (re.search(r"last \w+ ([^\s,]+)", clean(bl("capture", 2))) or [None, None])[1]
    post(f"/v1/devices/{dev}/capture", {"kind": "video"})
    t0 = time.time()
    bl("presence now", 1)
    base, sent, live = None, None, {"first": None, "ok": 0}

    def watch_live():
        """the live HLS of camera 1 while the upload runs: when its playlist first answers, and how often"""
        while sent is None and time.time() - t0 < SENT_S:
            if base:
                try:
                    with urllib.request.urlopen(f"{SERVER}/v1/live/{base}-v1/index.m3u8", timeout=5) as f:
                        if f.status == 200 and b"#EXTM3U" in f.read():
                            live["ok"] += 1
                            live["first"] = live["first"] or time.time() - t0
                except Exception:   # noqa: BLE001 - 404 before and after the upload
                    pass
            time.sleep(0.5)

    th = threading.Thread(target=watch_live, daemon=True)
    th.start()
    while time.time() - t0 < SENT_S:
        r.console += cmd(r.fd, None, 1)
        s = clean(bl("capture", 1.5))
        m = re.search(r"last video ([^\s,]+)", s)
        if m and m.group(1) == prev:
            m = None
        if m and base is None:
            base = m.group(1)
        if m and "one running" not in s:
            sent = time.time() - t0
            break
    th.join(6)
    t_end = time.time() + 5
    while time.time() < t_end and "media: done" not in (r.console.split(f"media: video {base}")[-1] if base else ""):
        r.console += cmd(r.fd, None, 1)
    seg = r.console.split(f"media: video {base}")[-1] if base else ""
    check(f"round {rnd}: SENT within {SENT_S} s of the check-in", sent is not None,
          f"{sent:.1f} s, {base}" if sent else f"none ({base})")
    done = re.findall(r"media: done, (\d+) uploaded, (\d+) failed", seg)
    check(f"round {rnd}: the K230 uploaded 2 videos, failed 0", done and done[0] == ("2", "0"),
          f"{done[0][0]} uploaded, {done[0][1]} failed" if done else "no media: done line")
    for line in re.findall(r"media: v\d: [^\n]*", seg):
        log("K230", line)
    check(f"round {rnd}: live HLS of camera 1 answered while it was recorded", live["ok"] > 0,
          f"first at {live['first']:.1f} s after the ask, {live['ok']} answers" if live["first"] else "never")
    if not base:
        continue
    takens = []
    for n, cam in ((1, "J4"), (2, "J5")):
        data, taken = fetch(f"{base}-v{n}")
        if data is None:
            check(f"round {rnd}: v{n} ({cam}) at the server", False, f"HTTP {taken}")
            continue
        path = os.path.join(r.dir, f"v{n}_{cam}.mp4")
        with open(path, "wb") as f:
            f.write(data)
        info, err = probe(path)
        st = (info.get("streams") or [{}])[0]
        fmt = info.get("format", {})
        frames = int(st.get("nb_read_frames") or 0)
        dur = float(fmt.get("duration") or 0)
        num, _, den = (st.get("avg_frame_rate") or "0/1").partition("/")
        fps = float(num) / float(den or 1) if float(den or 1) else 0.0
        ok = (st.get("codec_name") == "hevc" and st.get("width") == 2592 and st.get("height") == 1936
              and SECS - 0.5 <= dur <= SECS + 1.5 and frames > 0 and not err)   # the 2nd camera's first frame
                                                                  # may be one from before the 1st's: up to ~1 s more
        check(f"round {rnd}: v{n} ({cam}) HEVC 2592x1936, {SECS} s, every frame decodes", ok,
              f"{len(data)} B ({len(data) * 8 / max(dur, 0.1) / 1000:.0f} kbit/s), {st.get('codec_name')} "
              f"{st.get('codec_tag_string')} {st.get('width')}x{st.get('height')}, {frames} frames, {dur:.2f} s, "
              f"{fps:.1f} fps{', ' + err[:120] if err else ''}")
        check(f"round {rnd}: v{n} ({cam}) at {FPS} fps (the user's target; the cameras' own times)", fps >= FPS - 0.5,
              f"{fps:.1f} fps")
        created = fmt.get("tags", {}).get("creation_time", "")
        check(f"round {rnd}: v{n} taken now (X-Taken within 180 s, the MP4's creation time the same second)",
              taken and abs(taken / 1000 - time.time()) < 180 and created.startswith(
                  time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(taken // 1000))),
              f"X-Taken {taken}, creation_time {created}")
        takens.append(taken)
    if len(takens) == 2:
        check(f"round {rnd}: both cameras' videos from the same start (X-Taken within 1 s)",
              abs(takens[0] - takens[1]) < 1000, f"{takens[1] - takens[0]} ms apart")

states = []
for i in range(3):
    o = clean(bl("presence now", 25, r'check of "[^"]*": state[^\n]*\n'))
    m = re.search(r'check of "[^"]*": state (0x\w+)', o)
    states.append(m.group(1) if m else "none")
check("3 check-ins after the videos: all reach the server (0x03)", states == ["0x03"] * 3, " ".join(states))
r.clog_write("K230 console during the rounds", r.console)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
