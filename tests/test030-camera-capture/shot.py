#!/usr/bin/env python3
"""test030: a picture from the board's camera(s) on the PC. Runs /sdcard/app/cam_shot on the K230 console (the test
image: tests/firmware/test_install.sh) and writes each frame it prints as tests/test030-camera-capture/cam<slot>.png
(colour from the NV12 frame; grayscale from an older cam_shot that sends the luminance only).

    tests/test030-camera-capture/shot.py [cams] [settle] [white%[,ir%]]
        cams "0" (J4), "1" (J5) or "01"; settle frames (AE); the board's own LED strips on for the shot (test009:
        white = PWM 1, IR = PWM 0, 25 kHz), both off afterwards
"""
import base64
import os
import re
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "power-bringup"))
from board import cmd, k230, log, media_quiet   # noqa: E402

cams = sys.argv[1] if len(sys.argv) > 1 else "0"
settle = sys.argv[2] if len(sys.argv) > 2 else "20"
light = [int(x) for x in sys.argv[3].split(",")] + [0] if len(sys.argv) > 3 else [0, 0]
PERIOD = 40000
fd = k230()
if fd is None:
    sys.exit("no K230 console")
media_quiet(fd)                         # the agent's boot capture hook must not run at the same time (board.py)


def leds(white, ir):
    # both channels set every time: a channel never configured lit at full (test009)
    for ch, pct in ((1, white), (0, ir)):
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
    out = cmd(fd, f"/sdcard/app/cam_shot {cams} {settle}", 120, r"msh />\s*$")   # the prompt comes back at its end
finally:
    if any(light[:2]):
        leds(0, 0)
out = re.sub(r"\x1b\[[0-9;]*m", "", out).replace("\r", "")
open(os.path.join(HERE, "shot_console.txt"), "w").write(out)        # the raw output, for a decode fault
log(f"console output {len(out)} B in {time.time() - t0:.1f} s")
for line in out.splitlines():
    if line.startswith("cam_shot") or "vicap" in line:
        log(line)
from PIL import Image   # noqa: E402
for m in re.finditer(r"CAMSHOT (\d+) (\d+) (\d+) (\d+) ?(nv12|y)?\n(.*?)CAMSHOT END", out, re.S):
    slot, w, h, zlen = map(int, m.groups()[:4])
    fmt = m.group(5) or "y"
    # only whole base64 lines: other writers on the console (the BL616 link, kernel messages) put their own lines
    # in between (21 stray bytes on the first run, 2026-11-11)
    # the test agent's own messages can land inside a base64 line ("leakcam_agent: READY not answered": the BL616 test
    # firmware does not answer its READY, 2026-11-11): cut them out by their known starts, the data around them joins
    block = re.sub(r"(leakcam_agent:|link rx:)[^\n]*", "", m.group(6))
    lines = [ln.strip() for ln in block.splitlines()]
    bad = [ln for ln in lines if ln and not re.fullmatch(r"[A-Za-z0-9+/]+=*", ln)]
    if bad:
        log(f"cam {slot}: {len(bad)} foreign lines dropped: {bad[:3]}")
    data = base64.b64decode("".join(ln for ln in lines if re.fullmatch(r"[A-Za-z0-9+/]+=*", ln)))
    if len(data) != zlen:
        log(f"cam {slot}: {len(data)} of {zlen} compressed bytes arrived")
        continue
    raw = zlib.decompress(data)
    # the board's copy goes once the PC has it: left on /sdcard they filled it and test011's 5 MB copy failed
    # (2026-11-11 23:16, 11 MB of cam*.nv12 / cam*.h265)
    cmd(fd, f"rm /sdcard/cam{slot}.{'nv12' if fmt == 'nv12' else 'y'}", 1)
    path = os.path.join(HERE, f"cam{slot}.png")
    import numpy as np
    yp = np.frombuffer(raw[:w * h], np.uint8).reshape(h, w)
    if fmt == "nv12":                       # BT.601 limited range, the ISP's NV12 (YUV420 semi-planar, U first)
        uv = np.frombuffer(raw[w * h:w * h * 3 // 2], np.uint8).reshape(h // 2, w // 2, 2)
        u = uv[:, :, 0].repeat(2, 0).repeat(2, 1).astype(np.float32) - 128
        v = uv[:, :, 1].repeat(2, 0).repeat(2, 1).astype(np.float32) - 128
        yf = (yp.astype(np.float32) - 16) * 1.164
        rgb = np.stack([yf + 1.596 * v, yf - 0.392 * u - 0.813 * v, yf + 2.017 * u], -1)
        Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(path)
    else:
        Image.fromarray(yp, "L").save(path)
    # sharpness for focusing: the variance of the Laplacian (higher = sharper), over the whole frame and the centre
    a = yp.astype(np.float32)

    def lap_var(x):
        lap = x[1:-1, :-2] + x[1:-1, 2:] + x[:-2, 1:-1] + x[2:, 1:-1] - 4 * x[1:-1, 1:-1]
        return float(lap.var())
    c = a[h // 4:3 * h // 4, w // 4:3 * w // 4]
    log(f"cam {slot}: {w}x{h} -> {path}; sharpness {lap_var(a):.1f} (centre {lap_var(c):.1f})")
