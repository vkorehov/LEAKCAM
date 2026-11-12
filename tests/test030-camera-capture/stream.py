#!/usr/bin/env python3
"""test030: real H.265 from both cameras. Runs `leakcam_stream -f` on the K230 (the test image: camera -> VICAP -> ISP
-> VENC bound in hardware, no network; each camera to /sdcard/cam<N>.h265), pulls each file over the console with
fdump, checks it with ffprobe on the PC and saves its last frame as cam<N>_h265.png.

    tests/test030-camera-capture/stream.py [seconds] [fps] [white%[,ir%]]     defaults 10 s, 15 fps, LEDs off

The speed numbers: frames, fps and kbit/s per camera as the board counted them (STREAMREC lines), the frames ffprobe
finds, and the console transfer time.
"""
import base64
import json
import os
import re
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "power-bringup"))
from board import cmd, k230, log, media_quiet   # noqa: E402

secs = int(sys.argv[1]) if len(sys.argv) > 1 else 10
fps = int(sys.argv[2]) if len(sys.argv) > 2 else 15
light = [int(x) for x in sys.argv[3].split(",")] + [0] if len(sys.argv) > 3 else [0, 0]
PERIOD = 40000
PROMPT = r"msh />\s*$"
fd = k230()
if fd is None:
    sys.exit("no K230 console")
media_quiet(fd)                         # the agent's boot capture hook must not run at the same time (board.py)


def clean(out):
    return re.sub(r"\x1b\[[0-9;]*m", "", out).replace("\r", "")


def leds(white, ir):
    for ch, pct in ((1, white), (0, ir)):           # both set every time (test009: an unset channel lit at full)
        if pct:
            cmd(fd, f"pwm_set pwm {ch} {PERIOD} {PERIOD * pct // 100}", 1)
            cmd(fd, f"pwm_enable pwm {ch}", 1)
        else:
            cmd(fd, f"pwm_disable pwm {ch}", 1)


t0 = time.time()
try:
    if any(light[:2]):
        leds(*light[:2])
        log(f"LEDs white {light[0]} %, IR {light[1]} %")
    out = clean(cmd(fd, f"/sdcard/app/leakcam_stream -f -t {secs} -r {fps}", secs + 60, PROMPT))
finally:
    if any(light[:2]):
        leds(0, 0)
log(f"recorded {secs} s at {fps} fps in {time.time() - t0:.1f} s (program start to end)")
recs = re.findall(r"STREAMREC (cam\d) frames (\d+) idr (\d+) bytes (\d+) fps ([\d.]+) kbps (\d+)", out)  # + gap_ms, saved
for line in out.splitlines():
    if "STREAMREC" in line or "fail" in line or "cam" in line and ":" in line:
        log(line.strip())
if not recs:
    open(os.path.join(HERE, "stream_console.txt"), "w").write(out)
    sys.exit("no STREAMREC lines: see stream_console.txt")
for cam, *_ in recs:
    t1 = time.time()
    out = clean(cmd(fd, f"/sdcard/app/fdump /sdcard/{cam}.h265", 180, PROMPT))
    m = re.search(r"FDUMP (\d+) (\d+)\n(.*?)FDUMP END", out, re.S)
    if not m:
        log(f"{cam}: no FDUMP block")
        continue
    size, zlen = int(m.group(1)), int(m.group(2))
    block = re.sub(r"(leakcam_agent:|link rx:)[^\n]*", "", m.group(3))   # other writers on the console (shot.py)
    data = base64.b64decode("".join(ln.strip() for ln in block.splitlines()
                                    if re.fullmatch(r"[A-Za-z0-9+/]+=*", ln.strip())))
    if len(data) != zlen:
        log(f"{cam}: {len(data)} of {zlen} compressed bytes arrived")
        continue
    raw = zlib.decompress(data)
    # the board's copy goes once the PC has it: 4 MB per camera on a /sdcard with ~15 MB free filled it and test011's
    # 5 MB copy failed (2026-11-11 23:16)
    cmd(fd, f"rm /sdcard/{cam}.h265", 1)
    path = os.path.join(HERE, f"{cam}.h265")
    open(path, "wb").write(raw)
    dt = time.time() - t1
    log(f"{cam}: {size} B pulled in {dt:.1f} s ({size * 8 / 1000 / dt:.0f} kbit/s over the console) -> {path}")
    probe = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                            "stream=codec_name,profile,width,height,nb_read_frames", "-of", "json", path],
                           capture_output=True, text=True)
    st = (json.loads(probe.stdout or "{}").get("streams") or [{}])[0]
    log(f"{cam}: ffprobe {st.get('codec_name')} {st.get('profile')} {st.get('width')}x{st.get('height')}, "
        f"{st.get('nb_read_frames')} frames{'; ' + probe.stderr.strip()[:200] if probe.stderr.strip() else ''}")
    png = os.path.join(HERE, f"{cam}_h265.png")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-sseof", "-0.5", "-i", path, "-frames:v", "1", png],
                   capture_output=True)
    if os.path.exists(png):
        log(f"{cam}: last frame -> {png}")
