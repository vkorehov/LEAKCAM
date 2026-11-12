#!/usr/bin/env python3
"""test030 cases: every K230 media program after every other in one boot, so a video buffer (VB) state one leaves
behind shows in the next. 2026-11-11: leakcam_stream never destroyed its audio pool, kd_mpi_vb_exit() failed, and the
next program got "VB is initialized!" / "vb init failed" (test012 run 2) or "video buffer pools failed" (cam_shot after
several runs); fixed in leakcam_stream.c vb_teardown().

    tests/test030-camera-capture/vb_cases.py [rounds] [prog,prog]   default 2 rounds of every ordered pair; the names
                                                                     limit the pairs to those programs

Programs: still J4, still J5, both stills, mic 2 s, H.265 both cameras 2 s. Each ordered pair (A then B) runs in a row;
a step fails on a non-zero program result, a VB/pool message, or its missing result line. Board files are removed after
each step (/sdcard holds ~20 MB, test011).
"""
import itertools
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "power-bringup"))
from board import cmd, k230, log, media_quiet   # noqa: E402

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 2
ONLY = sys.argv[2].split(",") if len(sys.argv) > 2 else None     # e.g. still0,still01: only pairs of these
PROMPT = r"msh />\s*$"
BAD = re.compile(r"vb init failed|VB is initialized|pools? failed|vb exit failed|not destroyed|no frame|start failed|"
                 r"init failed")
PROGS = {   # name: (command, line that proves it worked, board files to remove)
    "still0": ("/sdcard/app/cam_shot 0 5", r"cam_shot: cam 0 1280x960", ["/sdcard/cam0.nv12"]),
    "still1": ("/sdcard/app/cam_shot 1 5", r"cam_shot: cam 1 1280x960", ["/sdcard/cam1.nv12"]),
    "still01": ("/sdcard/app/cam_shot 01 5", r"cam_shot: cam 1 1280x960", ["/sdcard/cam0.nv12", "/sdcard/cam1.nv12"]),
    "mic": ("/sdcard/app/leakcam_stream -m 2", r"mic\s+[\d.]+ s: rms", ["/sdcard/mic.raw"]),
    "h265": ("/sdcard/app/leakcam_stream -f -t 2", r"STREAMREC cam1 frames [1-9]", ["/sdcard/cam0.h265",
                                                                                     "/sdcard/cam1.h265"]),
}
fd = k230()
if fd is None:
    sys.exit("no K230 console")
log(f"agent stopped, no capture hook running ({media_quiet(fd):.1f} s)")   # its boot hook collided at 01:15 (board.py)


def step(name):
    c, ok_re, files = PROGS[name]
    out = re.sub(r"\x1b\[[0-9;]*m", "", cmd(fd, c, 60, PROMPT)).replace("\r", "")
    out = re.sub(r"(?m)^[A-Za-z0-9+/=]{40,}$\n?", "", out)        # cam_shot's base64 picture: not needed here
    for f in files:
        cmd(fd, f"rm {f}", 1)
    bad = BAD.findall(out)
    if bad or not re.search(ok_re, out):
        with open(os.path.join(HERE, "vb_cases_fail.txt"), "a") as f:     # the whole output of every failed step
            f.write(f"===== {time.strftime('%H:%M:%S')} {c}\n{out}\n")
        return f"{name}: {', '.join(sorted(set(bad))) or 'no result line'}"
    return None


fails, n = [], 0
t0 = time.time()
for r in range(ROUNDS):
    for a, b in itertools.product(ONLY or PROGS, repeat=2):
        for name in (a, b):
            n += 1
            e = step(name)
            if e:
                fails.append(f"round {r + 1}, {a} -> {b}: {e}")
                log(f"FAIL {fails[-1]}")
    log(f"round {r + 1}: {n} steps so far, {len(fails)} failed, {time.time() - t0:.0f} s")
free = re.findall(r"heap\s+\d+\s+\d+\s+(\d+)", cmd(fd, "free", 2))
log(f"summary: {n} steps, {len(fails)} failed; K230 heap available {free[-1] if free else '?'} B; "
    f"{'PASS' if not fails else 'FAIL'}")
sys.exit(1 if fails else 0)
