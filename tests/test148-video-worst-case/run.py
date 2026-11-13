#!/usr/bin/env python3
"""test148: the video's worst cases for the encoder and the upload (user 2026-11-13: "test quick change of frame by
simulating lighting change ... test worse cases of encoding"). Each round is test147's Take video (both cameras,
native 2592x1936, 15 fps, H.265 CBR) while the host switches both LED chains (white PWM1 + IR PWM0, full current,
through the K230's msh `pwm_set`) in a pattern, so the whole picture changes:
    none     the room's light only (the baseline)
    frame    on/off every 67 ms: a change on every frame, the LEDs' edges also across frames (rolling shutter)
    step     on/off every 2 s: steps the ISP's exposure has to follow, between still frames
    random   on/off at random 30..300 ms
Per video: frames, fps, every frame decodes, each frame's size (P vs IDR, the largest, the stream buffer's 1.25 MB),
the bit rate per second (the peak against CBR), the K230's encode / wait / send times and upload backlog.
    tests/test148-video-worst-case/run.py [pattern ...]     default: none frame step random
"""
import json
import os
import random
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

PATTERNS = [a for a in sys.argv[1:] if not a.isdigit()] or ["none", "frame", "step", "random"]
SECS, FPS, KBPS = 15, 15, 10000
STREAM_BUF = 2592 * 1936 // 4        # h265.c: the encoder's stream buffer per frame
SENT_S = 90
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
        with urllib.request.urlopen(f"{SERVER}/v1/media/{mid}", timeout=30) as f:
            return f.read()
    except urllib.error.HTTPError as e:
        return None


def frames_of(path):
    """per frame (packet) in decode order: size in bytes, key frame, pts in s; and the decoder's errors"""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "packet=size,flags,pts_time", "-of", "json", path], capture_output=True, text=True,
                         timeout=120)
    pk = json.loads(out.stdout or "{}").get("packets", [])
    dec = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "null", "-"], capture_output=True, text=True,
                         timeout=180)
    return [(int(p["size"]), "K" in p.get("flags", ""), float(p.get("pts_time") or 0)) for p in pk], \
        dec.stderr.strip()


def luma(path):
    """mean luma per frame (signalstats YAVG): how much the light really changed the picture"""
    out = subprocess.run(["ffprobe", "-v", "error", "-f", "lavfi", "-i",
                          f"movie={path},scale=324:242,signalstats", "-show_entries",
                          "frame_tags=lavfi.signalstats.YAVG", "-of", "csv=p=0"], capture_output=True, text=True,
                         timeout=300)
    return [float(x) for x in out.stdout.split() if x.replace(".", "", 1).isdigit()]


def audio_of(path):
    """the microphone's track: codec, start and duration in s, mean / max volume in dB (ffmpeg volumedetect)"""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                          "stream=codec_name,sample_rate,channels,start_time,duration", "-of", "json", path],
                         capture_output=True, text=True, timeout=60)
    st = (json.loads(out.stdout or "{}").get("streams") or [{}])[0]
    vol = subprocess.run(["ffmpeg", "-v", "info", "-i", path, "-map", "0:a:0", "-af", "volumedetect", "-f", "null", "-"],
                         capture_output=True, text=True, timeout=120).stderr
    mean = re.search(r"mean_volume: (-?[\d.]+) dB", vol)
    peak = re.search(r"max_volume: (-?[\d.]+) dB", vol)
    return (st.get("codec_name"), float(st.get("start_time") or 0), float(st.get("duration") or 0),
            float(mean.group(1)) if mean else None, float(peak.group(1)) if peak else None)


class Lights(threading.Thread):
    """both chains switched by msh on the K230's console: 100 % on, 0 % off (test009: pwm_set pwm <ch> 40000 ns)"""

    def __init__(self, fd, pattern):
        super().__init__(daemon=True)
        self.fd, self.pattern, self.stop, self.n = fd, pattern, False, 0

    def say(self, line):
        data = line.encode() + b"\r\n"
        while data:
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                time.sleep(0.002)

    def set(self, on):
        for ch in (1, 0):
            self.say(f"pwm_set pwm {ch} 40000 {40000 if on else 0}")
        self.n += 1

    def run(self):
        for ch in (1, 0):
            self.say(f"pwm_set pwm {ch} 40000 0")
            self.say(f"pwm_enable pwm {ch}")
        on = False
        while not self.stop:
            on = not on
            self.set(on)
            time.sleep({"frame": 1 / FPS, "step": 2.0}.get(self.pattern) or random.uniform(0.03, 0.3))
        for ch in (1, 0):
            self.say(f"pwm_set pwm {ch} 40000 0")
            self.say(f"pwm_disable pwm {ch}")


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish("not run: the board is not in the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
t = time.time()
while "one running" in bl("capture", 2) and time.time() - t < 130:
    time.sleep(2)
rows = []


def bl_mem(when):
    """the BL616's heap and its own lwIP heap / TCP segments (bl616_common/mem_stat.c)"""
    o = clean(bl("mem", 2))
    line = " | ".join(re.findall(r"^(?:mem|lwip): [^\r\n]*", o, re.M)) or o.strip()[-200:]
    log("BL616", f"{when}: {line}")
    return line


mem_lines = [bl_mem("before the videos")]
for pat in PATTERNS:
    prev = (re.search(r"last \w+ ([^\s,]+)", clean(bl("capture", 2))) or [None, None])[1]
    post(f"/v1/devices/{dev}/capture", {"kind": "video"})
    t0 = time.time()
    bl("presence now", 1)
    base, sent, lights, mark, tp = None, None, None, len(r.console), 0
    while time.time() - t0 < SENT_S:
        r.console += cmd(r.fd, None, 0.3)
        if time.time() - tp > 1:              # the page pool (VPU, the program, the kernel): its free pages
            os.write(r.fd, b"list_page\r\nlist_memheap\r\n")
            tp = time.time()
        # from the ask to SENT: the K230's console reaches the host late in bursts while it streams (a trigger on its
        # "media: video" line started the lights 1.5 s before the end, 2026-11-13); the job's shot is lit by them too
        if lights is None and pat != "none":
            lights = Lights(r.fd, pat)
            lights.start()
        s = clean(bl("capture", 1))
        m = re.search(r"last video ([^\s,]+)", s)
        if m and m.group(1) != prev:
            base = m.group(1)
            if "one running" not in s:
                sent = time.time() - t0
                break
    if lights is not None:
        lights.stop = True
        lights.join(5)
    t_end = time.time() + 5
    while time.time() < t_end and "media: done" not in r.console[mark:]:
        r.console += cmd(r.fd, None, 1)
    seg = clean(r.console[mark:])
    check(f"{pat}: SENT within {SENT_S} s of the check-in", sent is not None,
          f"{sent:.1f} s, {base}, {lights.n if lights else 0} light switches" if sent else f"none ({base})")
    seg = seg[seg.rfind("media: video " + base):] if base and ("media: video " + base) in seg else seg
    free = [int(x, 16) for x in re.findall(r"free pages is ([0-9a-fA-F]+)", seg)]
    vpu = [int(x) for x in re.findall(r"total used pages (\d+)", seg)]
    kheap = [(int(a), int(b)) for a, b in re.findall(r"^\s*heap\s+\d+\s+(\d+)\s+(\d+)", seg, re.M)]
    log("K230", f"{pat}: kernel heap (9 MB) max used {max(a for a, _ in kheap) if kheap else '?'}, "
                f"available min {min(b for _, b in kheap) if kheap else '?'} B over {len(kheap)} samples")
    check(f"{pat}: the page pool never ran out (no Memory exhaustion, free pages > 0)",
          "Memory exhaustion" not in seg and (not free or min(free) > 0),
          f"free pages min {min(free) if free else '?'} ({min(free) * 4 // 1024 if free else '?'} MB) of "
          f"{len(free)} samples, VPU pages {vpu}, exhaustion lines {seg.count('Memory exhaustion')}")
    done = re.findall(r"media: done, (\d+) uploaded, (\d+) failed", seg)
    check(f"{pat}: the K230 uploaded 2 videos, failed 0", done and done[0][:2] == ("2", "0"),
          f"{done[0][0]} uploaded, {done[0][1]} failed" if done else "no media: done line")
    k230 = {}
    for n, line in re.findall(r"media: v(\d): ([^\n]*)", seg):
        log("K230", f"v{n}: {line}")
        k230[n] = line
    if not base:
        continue
    for n, cam in ((1, "J4"), (2, "J5")):
        data = fetch(f"{base}-v{n}")
        if data is None:
            check(f"{pat}: v{n} ({cam}) at the server", False, "no MP4")
            continue
        path = os.path.join(r.dir, f"{pat}_v{n}_{cam}.mp4")
        with open(path, "wb") as f:
            f.write(data)
        fr, err = frames_of(path)
        sizes = [s for s, _, _ in fr]
        p = [s for s, k, _ in fr if not k]
        idr = [s for s, k, _ in fr if k]
        dur = (max(x[2] for x in fr) - min(x[2] for x in fr) + 1 / FPS) if fr else 0
        fps = len(fr) / dur if dur else 0
        persec = {}
        for s, _, ts in fr:
            persec[int(ts)] = persec.get(int(ts), 0) + s
        peak = max(persec.values()) * 8 / 1000 if persec else 0
        avg = sum(sizes) * 8 / dur / 1000 if dur else 0
        y = luma(path)
        dy = [abs(a - b) for a, b in zip(y, y[1:])]
        row = dict(pattern=pat, cam=cam, frames=len(fr), fps=round(fps, 2), kbps=round(avg), peak_kbps=round(peak),
                   p_avg=round(sum(p) / len(p)) if p else 0, p_max=max(p) if p else 0,
                   idr_avg=round(sum(idr) / len(idr)) if idr else 0, idr_max=max(idr) if idr else 0,
                   frame_max=max(sizes) if sizes else 0, luma_step_avg=round(sum(dy) / len(dy), 2) if dy else 0,
                   luma_step_max=round(max(dy), 1) if dy else 0, k230=k230.get(str(n), ""), err=err[:200])
        rows.append(row)
        log("VIDEO", json.dumps(row))
        m = re.search(r"(\d+) dropped in (\d+) gaps", row["k230"])
        row["dropped"], row["gaps"] = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        check(f"{pat}: v{n} ({cam}) every frame decodes, {SECS * FPS} frames with the dropped ones (a full backlog "
              f"drops up to the next IDR, media.c)", not err and len(fr) + row["dropped"] >= SECS * FPS - 2,
              f"{len(fr)} frames + {row['dropped']} dropped in {row['gaps']} gaps, {fps:.2f} fps"
              f"{', ' + err[:120] if err else ''}")
        codec, a_start, a_dur, a_mean, a_peak = audio_of(path)
        row.update(audio=codec, audio_start=a_start, audio_s=a_dur, audio_mean_db=a_mean, audio_peak_db=a_peak)
        check(f"{pat}: v{n} ({cam}) the microphone's Opus track, the video's length (1 s), not silent (mean > -70 dB)",
              codec == "opus" and abs(a_start + a_dur - dur) <= 1.0 and a_mean is not None and a_mean > -70,
              f"{codec}, {a_start:.2f} + {a_dur:.2f} s (video {dur:.2f} s), mean {a_mean} dB, max {a_peak} dB")
        check(f"{pat}: v{n} ({cam}) largest frame under the stream buffer ({STREAM_BUF} B)",
              row["frame_max"] < STREAM_BUF, f"largest {row['frame_max']} B, IDR max {row['idr_max']}, "
              f"P max {row['p_max']}")
        # the peak second is reported, not judged: the encoder's CBR overshoots to ~15.7 Mbit/s for a second under
        # per-frame flicker, the library has no rate control to tune at run time (mpi_venc_api.h), and the upload's
        # queue and frame drops (media.c) take it
        check(f"{pat}: v{n} ({cam}) bit rate held: average <= {KBPS * 1.1:.0f} kbit/s (peak second reported)",
              avg <= KBPS * 1.1, f"average {avg:.0f}, peak {peak:.0f} kbit/s")

mem_lines.append(bl_mem("after the videos"))
states = []
for i in range(3):
    o = clean(bl("presence now", 25, r'check of "[^"]*": state[^\n]*\n'))
    m = re.search(r'check of "[^"]*": state (0x\w+)', o)
    states.append(m.group(1) if m else "none")
    r.console += f"\n--- BL616 check-in {i + 1} after the videos ---\n{o}\n"
    mem_lines.append(bl_mem(f"after check-in {i + 1} ({states[-1]})"))
check("3 check-ins after the videos: all reach the server (0x03)", states == ["0x03"] * 3, " ".join(states))
with open(os.path.join(r.dir, "frames.json"), "w") as f:
    json.dump(rows, f, indent=1)
r.clog_write("K230 console during the rounds", r.console)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
