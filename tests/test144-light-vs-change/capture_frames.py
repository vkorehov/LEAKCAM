#!/usr/bin/env python3
"""test144 step 1: real frames of the bench scene, both cameras, once with the LEDs and once in the room's light alone
(the K230 job's `lit=led` / `lit=room` token in /sdcard/leakcam/sim), through the product's own photo path (the server's
Take photo -> SHOOT -> JPEG upload, tests/test143). Saved as frames/<UTC time>_<led|room>_cam<N>.jpg: the sets
lightsep.py builds its pairs from (a frame against itself under the other lighting, and against transforms of it).

    tests/test144-light-vs-change/capture_frames.py
"""
import json
import os
import re
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "power-bringup"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "test-runner"))
from board import bl, cmd, k230, log   # noqa: E402
from phone import SERVER, board_id, firmware   # noqa: E402

PROMPT = r"msh />\s*$"


def clean(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def photo(dev, fd):
    """one Take photo through the product; the two JPEGs (cam0, cam1) and the K230's lighting line"""
    prev = (re.search(r"last \w+ ([^\s,]+)", clean(bl("capture", 2))) or [None, None])[1]
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/capture", json.dumps({"kind": "photo"}).encode(),
                                 {"Content-Type": "application/json"})
    urllib.request.urlopen(req, timeout=5)
    bl("presence now", 1)
    t0, out = time.time(), ""
    while time.time() - t0 < 60:
        out += cmd(fd, None, 1)
        s = clean(bl("capture", 1.5))
        m = re.search(r"last photo ([^\s,]+)", s)
        if m and m.group(1) != prev and "one running" not in s:
            break
    else:
        return None, out
    pics = []
    for n in (1, 2):
        with urllib.request.urlopen(f"{SERVER}/v1/media/{m.group(1)}-p{n}", timeout=10) as f:
            pics.append(f.read())
    return pics, out


def main():
    if firmware() != "full":
        sys.exit("not run: the board is not in the full firmware")
    dev, fd = board_id(), k230()
    os.makedirs(os.path.join(HERE, "frames"), exist_ok=True)
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    if "No such file" in cmd(fd, "ls /sdcard/leakcam/sim", 2, PROMPT):
        bl("leak check", 25, r"leak: DONE")      # the job creates the file (the console's echo only appends)
    try:
        for mode in ("led", "room"):
            cmd(fd, f"echo lit={mode} /sdcard/leakcam/sim", 2, PROMPT)
            pics, out = photo(dev, fd)
            line = (re.findall(r"job: colour [^\n]*", clean(out)) or ["no colour line"])[-1]
            if not pics:
                log(f"{mode}: no photo within 60 s ({line})")
                continue
            if ("colour LEDs" in line) != (mode == "led"):
                sys.exit(f"{mode}: the shot was not in that light ({line}): not saved")
            for n, data in enumerate(pics):
                p = os.path.join(HERE, "frames", f"{stamp}_{mode}_cam{n}.jpg")
                with open(p, "wb") as f:
                    f.write(data)
            log(f"{mode}: {len(pics[0])} / {len(pics[1])} B; {line}")
    finally:
        cmd(fd, "rm /sdcard/leakcam/sim", 2, PROMPT)    # the forced lighting never outlives the capture


main()
