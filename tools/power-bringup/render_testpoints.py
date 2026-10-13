#!/usr/bin/env python3
"""Draw the power bring-up probe points on the MIFA board, from the fab ODB++.

  python3 render_testpoints.py [MIFA.tgz | git revision]   -> top.png, bottom.png next to this script

Reads "Project Outputs for LEAKCAM/MIFA.tgz" as committed in the fab package of the ordered board
(git 11a6a38, rev 1; the working tree may already hold later layout changes) by default, checks
every probe pad in testpoints.json against the netlist (the pad must be on its rail, the GND pad on
GND) and stops if one is not, so the pictures never show a wrong pad. Needs numpy and Pillow.
"""
import json
import math
import os
import re
import subprocess
import sys
import tarfile
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FAB_REV = "11a6a38"              # fab package of rev 1 (ordered 2026-09-29)
ODB_PATH = "Project Outputs for LEAKCAM/MIFA.tgz"
PX = 22.0                       # pixels per mm
MARGIN = 330                    # room for the labels left and right
COLORS = {"top": (196, 140, 40), "bottom": (60, 110, 190)}


def load_odb(tgz):
    tmp = tempfile.mkdtemp(prefix="mifa_odb_")
    with tarfile.open(tgz) as t:
        t.extractall(tmp, filter="data")
    for root, dirs, _ in os.walk(tmp):
        if os.path.basename(root) == "pcb" and "layers" in dirs:
            return root
    raise SystemExit(f"no steps/pcb in {tgz}")


def netlist(pcb):
    nets = [l.split()[1].split(";")[0] for l in open(os.path.join(pcb, "eda", "data"), errors="replace")
            if l.startswith("NET ")]
    comps = {}
    for side in ("top", "bot"):
        cur = None
        for l in open(os.path.join(pcb, "layers", f"comp_+_{side}", "components"), errors="replace"):
            if l.startswith("CMP"):
                f = l.split(";")[0].split()
                cur = f[6]
                comps[cur] = {"side": "top" if side == "top" else "bottom", "part": f[7], "pins": {}}
            elif l.startswith("TOP") and cur:
                f = l.split(";")[0].split()
                comps[cur]["pins"][f[-1]] = (float(f[2]), float(f[3]), nets[int(f[6])])
    return comps


def features(pcb, layer):
    """pads (x, y, shape, w, h, angle) and lines (x0, y0, x1, y1, width) of one layer, mm"""
    syms, pads, lines = {}, [], []
    for l in open(os.path.join(pcb, "layers", layer, "features"), errors="replace"):
        if l.startswith("$"):
            i, s = l.split()[:2]
            syms[i[1:]] = s
        elif l.startswith("P "):
            f = l.split(";")[0].split()
            orient = int(f[6]) if len(f) > 6 else 0
            ang = float(f[7]) if orient in (8, 9) and len(f) > 7 else (orient % 4) * 90.0
            s = syms.get(f[3], "")
            m = re.match(r"(rect|oval)([\d.]+)x([\d.]+)", s) or re.match(r"(r|s)([\d.]+)", s)
            if not m:
                continue
            w = float(m.group(2)) / 1000
            h = float(m.group(3)) / 1000 if m.lastindex >= 3 else w
            pads.append((float(f[1]), float(f[2]), m.group(1), w, h, ang))
        elif l.startswith("L "):
            f = l.split(";")[0].split()
            m = re.match(r"r([\d.]+)", syms.get(f[5], "r100"))
            lines.append((float(f[1]), float(f[2]), float(f[3]), float(f[4]), float(m.group(1)) / 1000 if m else 0.1))
    return pads, lines


def outline(pcb):
    pts = []
    for l in open(os.path.join(pcb, "profile")):
        if l.startswith(("OB", "OS")):
            f = l.split()
            pts.append((float(f[1]), float(f[2])))
    return pts


def render(pcb, comps, points, side, out, region=None, px=PX, label=""):
    pts = outline(pcb)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    x0, x1, y0, y1 = region or (min(xs), max(xs), min(ys), max(ys))
    PX = px
    flip = side == "bottom"            # bottom seen from below: mirror left-right
    W = int((x1 - x0) * PX) + 2 * MARGIN
    H = int((y1 - y0) * PX) + 160

    def P(x, y):
        u = (x1 - x) if flip else (x - x0)
        return MARGIN + u * PX, 110 + (y1 - y) * PX

    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 17)
        big = ImageFont.truetype("DejaVuSans-Bold.ttf", 26)
        small = ImageFont.truetype("DejaVuSans.ttf", 13)
    except OSError:
        font = big = small = ImageFont.load_default()
    d.polygon([P(x, y) for x, y in pts], fill=(232, 240, 228), outline=(40, 90, 40))
    inside = lambda x, y: x0 - 0.5 <= x <= x1 + 0.5 and y0 - 0.5 <= y <= y1 + 0.5
    copper = "top_layer" if side == "top" else "bottom_layer"
    silk = "top_overlay" if side == "top" else "bottom_overlay"
    pads, _ = features(pcb, copper)
    for x, y, shape, w, h, ang in pads:
        if not inside(x, y):
            continue
        a = math.radians(ang)
        c, s = math.cos(a), math.sin(a)
        corners = [(x + dx * c + dy * s, y - dx * s + dy * c) for dx, dy in
                   ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2))]
        if shape in ("r",):
            cx, cy = P(x, y)
            r = w / 2 * PX
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(205, 170, 120))
        else:
            d.polygon([P(*q) for q in corners], fill=(205, 170, 120))
    _, sl = features(pcb, silk)
    for xa, ya, xb, yb, wd in sl:
        if not (inside(xa, ya) and inside(xb, yb)):
            continue
        d.line((P(xa, ya), P(xb, yb)), fill=(120, 120, 120), width=max(1, int(wd * PX)))
    # reference labels for the parts that matter here
    for ref in ("U1", "U3", "U5", "U6", "U7", "U8", "U10", "U11", "PR1", "USBC1", "USBC2", "JB1", "JB2"):
        cp = comps.get(ref)
        if not cp or cp["side"] != side:
            continue
        if not all(inside(x, y) for x, y, _ in cp["pins"].values()):
            continue
        px = [P(x, y) for x, y, _ in cp["pins"].values()]
        cx, cy = sum(p[0] for p in px) / len(px), sum(p[1] for p in px) / len(px)
        d.text((cx, cy), ref, fill=(30, 30, 160), font=font, anchor="mm", stroke_width=3, stroke_fill="white")
    # probe points: a ring on the rail pad, a small ring on its GND pad, a numbered label at the
    # side nearer to the pad, at the pad's height (labels pushed apart so they do not overlap)
    pos = lambda key, p: P(*comps[p[key].split(".")[0]]["pins"][p[key].split(".")[1]][:2])
    mine = [p for p in points if p["side"] == side and inside(*comps[p["pad"].split(".")[0]]["pins"][p["pad"].split(".")[1]][:2])]
    left = sorted((p for p in mine if pos("pad", p)[0] < W / 2), key=lambda p: pos("pad", p)[1])
    right = sorted((p for p in mine if pos("pad", p)[0] >= W / 2), key=lambda p: pos("pad", p)[1])
    for group, lx in ((left, 18), (right, W - MARGIN + 30)):
        ys_ = [min(max(pos("pad", p)[1], 120), H - 60) for p in group]
        for _ in range(50):                     # relax: keep 64 px between labels
            for k in range(1, len(ys_)):
                if ys_[k] - ys_[k - 1] < 64:
                    m = (ys_[k] + ys_[k - 1]) / 2
                    ys_[k - 1], ys_[k] = m - 32, m + 32
            ys_ = [min(max(v, 120), H - 60) for v in ys_]
        for p, ly in zip(group, ys_):
            x, y = pos("pad", p)
            gx, gy = pos("gnd", p)
            col = (200, 30, 30)
            d.ellipse((gx - 6, gy - 6, gx + 6, gy + 6), outline=(20, 20, 20), width=2)
            d.line(((lx + 275 if lx < W / 2 else lx - 6), ly, x, y), fill=col, width=2)
            d.ellipse((x - 10, y - 10, x + 10, y + 10), outline=col, width=4)
            d.text((x + 13, y - 13), str(p["id"]), fill=col, font=font, stroke_width=3, stroke_fill="white")
            d.text((lx, ly - 22), f"{p['id']}  {p['rail']}", fill=col, font=big)
            d.text((lx, ly + 8), f"+ {p['pad']}   GND {p['gnd']}", fill=(20, 20, 20), font=small)
    title = f"MIFA rev 1 power bring-up, {side} side{label}" + (" (seen from below, mirrored)" if flip else "")
    d.text((W / 2, 30), title, fill=(0, 0, 0), font=big, anchor="mm")
    d.text((W / 2, 64), "red ring = probe (+), black ring = GND for the black lead; numbers match testpoints.json and power_check.py",
           fill=(60, 60, 60), font=small, anchor="mm")
    img.save(out)
    print("wrote", out, f"({len(mine)} probe points)")


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else FAB_REV
    if os.path.isfile(arg):
        tgz = arg
    else:                               # a git revision: the ODB++ exactly as committed there
        tgz = os.path.join(tempfile.mkdtemp(prefix="mifa_tgz_"), "MIFA.tgz")
        with open(tgz, "wb") as f:
            f.write(subprocess.run(["git", "-C", REPO, "show", f"{arg}:{ODB_PATH}"], check=True, capture_output=True).stdout)
    pcb = load_odb(tgz)
    comps = netlist(pcb)
    points = json.load(open(os.path.join(HERE, "testpoints.json")))["points"]
    bad = []
    for p in points:
        for key, want in (("pad", p["rail"]), ("gnd", "GND")):
            ref, pin = p[key].split(".")
            got = comps.get(ref, {}).get("pins", {}).get(pin, (0, 0, "missing"))[2]
            if got != want:
                bad.append(f"{p['id']} {p['rail']}: {p[key]} is on {got}, expected {want}")
    if bad:
        raise SystemExit("probe table does not match the board:\n  " + "\n  ".join(bad))
    for side in ("top", "bottom"):
        render(pcb, comps, points, side, os.path.join(HERE, f"{side}.png"))
    render(pcb, comps, points, "top", os.path.join(HERE, "top_power_zoom.png"), region=(43.0, 60.5, 24.0, 49.0),
           px=55.0, label=", power corner (zoom)")


if __name__ == "__main__":
    main()
