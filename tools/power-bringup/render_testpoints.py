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


def render_hookup(pcb, comps, items, side, out):
    """hand-soldered parts and connectors: every pad ringed, pin 1 marked, a label at the side"""
    pts = outline(pcb)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    flip = side == "bottom"
    LM, RM = 470, 700                       # label columns left and right of the board
    W = int((x1 - x0) * PX) + LM + RM
    H = int((y1 - y0) * PX) + 160

    def P(x, y):
        u = (x1 - x) if flip else (x - x0)
        return LM + u * PX, 110 + (y1 - y) * PX

    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 15)
        big = ImageFont.truetype("DejaVuSans-Bold.ttf", 18)
        title_f = ImageFont.truetype("DejaVuSans-Bold.ttf", 26)
    except OSError:
        font = big = title_f = ImageFont.load_default()
    d.polygon([P(x, y) for x, y in pts], fill=(232, 240, 228), outline=(40, 90, 40))
    pads, _ = features(pcb, "top_layer" if side == "top" else "bottom_layer")
    for x, y, shape, w, h, ang in pads:
        if shape == "r":
            cx, cy = P(x, y)
            r = w / 2 * PX
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(205, 170, 120))
            continue
        a = math.radians(ang)
        c, s_ = math.cos(a), math.sin(a)
        d.polygon([P(x + dx * c + dy * s_, y - dx * s_ + dy * c) for dx, dy in
                   ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2))], fill=(205, 170, 120))
    _, sl = features(pcb, "top_overlay" if side == "top" else "bottom_overlay")
    for xa, ya, xb, yb, wd in sl:
        d.line((P(xa, ya), P(xb, yb)), fill=(120, 120, 120), width=max(1, int(wd * PX)))
    refs_of = lambda it: [it["ref"]] + it.get("also", []) + (["B1_N"] if it["ref"] == "B1_P" else [])
    mine = [it for it in items if comps.get(it["ref"], {}).get("side") == side]

    def centre(it):
        q = [P(*v[:2]) for r in refs_of(it) for v in comps[r]["pins"].values()]
        return sum(a for a, _ in q) / len(q), sum(b for _, b in q) / len(q)
    bx0, bx1 = LM, W - RM
    left = sorted((it for it in mine if centre(it)[0] < (bx0 + bx1) / 2), key=lambda it: centre(it)[1])
    right = sorted((it for it in mine if centre(it)[0] >= (bx0 + bx1) / 2), key=lambda it: centre(it)[1])
    col = (20, 70, 180)
    for group, lx, anchor_x in ((left, 10, LM - 12), (right, bx1 + 16, bx1 + 10)):
        ys_ = [min(max(centre(it)[1], 120), H - 60) for it in group]
        for _ in range(60):
            for k in range(1, len(ys_)):
                if ys_[k] - ys_[k - 1] < 50:
                    m = (ys_[k] + ys_[k - 1]) / 2
                    ys_[k - 1], ys_[k] = m - 25, m + 25
            ys_ = [min(max(v, 120), H - 40) for v in ys_]
        for it, ly in zip(group, ys_):
            for ref in refs_of(it):
                for pin, (x, y, _) in comps[ref]["pins"].items():
                    cx, cy = P(x, y)
                    d.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), outline=col, width=3)
                    if pin == "1" and len(comps[ref]["pins"]) > 1:
                        d.text((cx + 9, cy - 20), "1", fill=(200, 30, 30), font=big, stroke_width=3, stroke_fill="white")
            cx, cy = centre(it)
            d.line((anchor_x, ly, cx, cy), fill=col, width=2)
            d.text((lx, ly - 18), it["label"], fill=col, font=big)
            if it["note"]:
                d.text((lx, ly + 4), it["note"], fill=(40, 40, 40), font=font)
    d.text((W / 2, 30), f"MIFA rev 1: hand-soldered parts and connectors, {side} side" + (" (seen from below, mirrored)" if flip else ""),
           fill=(0, 0, 0), font=title_f, anchor="mm")
    d.text((W / 2, 64), "blue rings = pads, red 1 = pin 1; the LED strips are the four panels on the left (cut apart by hand)",
           fill=(60, 60, 60), font=font, anchor="mm")
    img.save(out)
    print("wrote", out, f"({len(mine)} parts)")

def draw_led_wiring(out):
    """the two LED chains in series: J1 -> strips A..D white pairs -> J1, J2 -> IR pairs -> J2"""
    W, H = 1700, 760
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype("DejaVuSans-Bold.ttf", 22)
        fs = ImageFont.truetype("DejaVuSans-Bold.ttf", 15)
    except OSError:
        f = fs = ImageFont.load_default()
    WHITE, IR, PCB, PAD = (225, 145, 0), (170, 20, 140), (232, 240, 228), (205, 170, 120)

    def pad(x, y, n, sign, col):
        d.ellipse((x - 13, y - 13, x + 13, y + 13), fill=PAD, outline=(90, 70, 40), width=2)
        d.text((x, y), n, fill=(0, 0, 0), font=fs, anchor="mm")
        d.text((x, y - 30), sign, fill=col, font=f, anchor="mm")

    def led(x, y, col):
        d.polygon([(x - 15, y - 14), (x - 15, y + 14), (x + 12, y)], fill=col)
        d.line((x + 12, y - 14, x + 12, y + 14), fill=col, width=4)
        for k in (0, 10):
            d.line((x + 3 + k, y - 18, x + 11 + k, y - 29), fill=col, width=2)

    def chain(y, col, jname, strips, plus, minus):
        # board connector on the left: pin 1 = + out, pin 2 = return
        d.rectangle((30, y - 95, 210, y + 135), fill=PCB, outline=(40, 90, 40), width=3)
        d.text((120, y - 70), jname, fill=col, font=f, anchor="mm")
        pad(170, y, "1", "+", col)
        pad(170, y + 80, "2", "-", col)
        x = 290
        prev = (183, y)
        for name, ref in strips:
            d.rectangle((x, y - 75, x + 300, y + 45), fill=PCB, outline=(40, 90, 40), width=3)
            d.text((x + 150, y - 55), f"{name}  {ref}", fill=(40, 90, 40), font=f, anchor="mm")
            a, b = (x + 35, y), (x + 265, y)
            pad(*a, plus, "+", col)
            pad(*b, minus, "-", col)
            d.line((a[0] + 13, y, b[0] - 13, y), fill=col, width=3)          # copper on the strip
            led(x + 115, y, col)
            led(x + 190, y, col)
            d.line((prev[0], prev[1], a[0] - 13, y), fill=col, width=6)       # wire
            prev = (b[0] + 13, y)
            x += 350
        # return wire from the last strip back to pin 2
        d.line([prev, (prev[0] + 40, y), (prev[0] + 40, y + 80), (183, y + 80)], fill=col, width=6, joint="curve")

    chain(140, WHITE, "J1 white", [("A", "J3"), ("B", "J7"), ("C", "J9"), ("D", "J11")], "2", "1")
    chain(520, IR, "J2 IR", [("A", "J6"), ("B", "J8"), ("C", "J10"), ("D", "J12")], "1", "2")
    img.save(out)
    print("wrote", out)


def draw_pr1(pcb, comps, out):
    """PR1 (bottom side, seen from below): each pad named, USB cable and BOOT/EN buttons wired"""
    pins = comps["PR1"]["pins"]
    xs = [v[0] for v in pins.values()]
    ys = [v[1] for v in pins.values()]
    cx, cy = sum(xs) / 6, sum(ys) / 6
    S, Wd, Hd = 66.0, 1500, 900
    x_left = 360                                    # board area on the left
    def P(x, y):                                    # mirrored (seen from below), PR1 in the middle of the board area
        return x_left + (cx - x) * S, Hd / 2 + (cy - y) * S
    img = Image.new("RGB", (Wd, Hd), "white")
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype("DejaVuSans-Bold.ttf", 22)
        fs = ImageFont.truetype("DejaVuSans.ttf", 17)
    except OSError:
        f = fs = ImageFont.load_default()
    d.rectangle((40, 60, 700, Hd - 30), fill=(232, 240, 228), outline=(40, 90, 40), width=3)
    d.text((370, 30), "PR1, bottom side (seen from below)", fill=(40, 90, 40), font=f, anchor="mm")
    pads, _ = features(pcb, "bottom_layer")
    for x, y, shape, w, h, ang in pads:
        if abs(x - cx) > 4.5 or abs(y - cy) > 6:
            continue
        px, py = P(x, y)
        r = max(w, h) / 2 * S
        if shape == "r":
            d.ellipse((px - r, py - r, px + r, py + r), fill=(205, 170, 120))
        else:
            r2 = min(w, h) / 2 * S
            d.rectangle((px - r2, py - r2, px + r2, py + r2), fill=(205, 170, 120))
    names = {"1": ("D+", (0, 150, 0)), "2": ("D-", (90, 90, 90)), "3": ("GND", (0, 0, 0)),
             "4": ("BOOT", (0, 90, 200)), "5": ("EN", (200, 100, 0)), "6": ("3V3_SLEEP", (200, 0, 0))}
    pos = {}
    for n, (x, y, _) in pins.items():
        px, py = P(x, y)
        pos[n] = (px, py)
        d.ellipse((px - 16, py - 16, px + 16, py + 16), outline=(20, 70, 180), width=4)
        d.text((px - 30, py), n, fill=(200, 30, 30), font=f, anchor="rm")
        d.text((px + 6, py - 26), names[n][0], fill=names[n][1], font=f, anchor="lb")
    # USB cable on the right: D+ green, D- white, GND black; VBUS red cut back and taped
    ux = 1250
    d.rectangle((ux, 330, ux + 200, 470), outline=(0, 0, 0), width=4)
    d.text((ux + 100, 400), "USB-A to PC", fill=(0, 0, 0), font=f, anchor="mm")
    for k, (n, col, label) in enumerate((("1", (0, 150, 0), "green D+"), ("2", (150, 150, 150), "white D-"),
                                         ("3", (0, 0, 0), "black GND"))):
        px, py = pos[n]
        yy = 350 + k * 40
        d.line([(px + 20, py), (1000 - k * 30, py), (1000 - k * 30, yy), (ux, yy)], fill=col, width=6)
        d.text((ux - 10, yy - 14), label, fill=col, font=fs, anchor="rb")
    d.line((ux, 470 - 10, 1120, 470 - 10), fill=(220, 0, 0), width=6)
    d.text((1100, 470 - 10), "red VBUS: cut, tape", fill=(220, 0, 0), font=fs, anchor="rm")
    # BOOT: a wire link from pad 4 to pad 6 (3V3_SLEEP) while flashing; EN: a wire on pad 5 whose
    # free end is touched to GND (pad 3) for a moment = reset (dashed: not a permanent connection)
    blue, orange = (0, 90, 200), (200, 100, 0)
    (x6, y6), (x4, y4) = pos["6"], pos["4"]
    d.line([(x6 - 18, y6), (230, y6), (230, y4), (x4 - 18, y4)], fill=blue, width=6)
    d.text((215, (y6 + y4) / 2), "link 4-6:\nboot into\nflash mode", fill=blue, font=fs, anchor="rm")
    (x5, y5), (x3, y3) = pos["5"], pos["3"]
    d.line([(x5 + 18, y5), (600, y5), (600, (y5 + y3) / 2)], fill=orange, width=6)
    yy = (y5 + y3) / 2
    while yy < y3 - 10:
        d.line((600, yy, 600, min(yy + 14, y3 - 10)), fill=orange, width=6)
        yy += 26
    d.ellipse((592, y3 - 18, 608, y3 - 2), outline=orange, width=3)
    d.text((615, (y5 + y3) / 2), "touch 5 to GND\n= reset", fill=orange, font=fs, anchor="lm")
    img.save(out)
    print("wrote", out)

def draw_led_wiring_board(pcb, comps, out):
    """the LED chains drawn on the real panel: wires between the actual pads of J1/J2 and the strips"""
    x0, x1, y0, y1 = -0.5, 46.5, 24.0, 52.0
    S = 40.0
    W, H = int((x1 - x0) * S) + 80, int((y1 - y0) * S) + 120
    def P(x, y):
        return 40 + (x - x0) * S, 80 + (y1 - y) * S
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype("DejaVuSans-Bold.ttf", 22)
        fs = ImageFont.truetype("DejaVuSans-Bold.ttf", 18)
    except OSError:
        f = fs = ImageFont.load_default()
    d.rectangle((*P(x0, y1), *P(x1, y0)), fill=(232, 240, 228))
    inside = lambda x, y: x0 <= x <= x1 and y0 <= y <= y1
    pads, _ = features(pcb, "top_layer")
    for x, y, shape, w, h, ang in pads:
        if not inside(x, y):
            continue
        if shape == "r":
            cx, cy = P(x, y)
            r = w / 2 * S
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(205, 170, 120))
            continue
        a = math.radians(ang)
        c, s_ = math.cos(a), math.sin(a)
        d.polygon([P(x + dx * c + dy * s_, y - dx * s_ + dy * c) for dx, dy in
                   ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2))], fill=(205, 170, 120))
    _, sl = features(pcb, "top_overlay")
    for xa, ya, xb, yb, wd in sl:
        if inside(xa, ya) and inside(xb, yb):
            d.line((P(xa, ya), P(xb, yb)), fill=(120, 120, 120), width=max(1, int(wd * S)))
    WHITE, IR = (225, 145, 0), (170, 20, 140)
    pin = lambda ref, n: P(*comps[ref]["pins"][n][:2])

    def wire(a, b, col, bend):
        (ax, ay), (bx, by) = a, b
        # quadratic curve through a control point pushed sideways, so the wires stay apart
        mx, my = (ax + bx) / 2, (ay + by) / 2 + bend
        pts = [((1 - t) ** 2 * ax + 2 * (1 - t) * t * mx + t * t * bx,
                (1 - t) ** 2 * ay + 2 * (1 - t) * t * my + t * t * by) for t in [k / 40 for k in range(41)]]
        d.line(pts, fill=col, width=7, joint="curve")

    white = [("J1", "1"), ("J3", "2"), ("J3", "1"), ("J7", "2"), ("J7", "1"), ("J9", "2"), ("J9", "1"),
             ("J11", "2"), ("J11", "1"), ("J1", "2")]
    ir = [("J2", "1"), ("J6", "1"), ("J6", "2"), ("J8", "1"), ("J8", "2"), ("J10", "1"), ("J10", "2"),
          ("J12", "1"), ("J12", "2"), ("J2", "2")]
    for chain, col, bend in ((white, WHITE, -150), (ir, IR, 150)):
        for k in range(0, len(chain), 2):           # wires: board -> A+, A- -> B+, ..., D- -> board
            a, b = pin(*chain[k]), pin(*chain[k + 1])
            wire(a, b, col, bend if k in (0, len(chain) - 2) else bend * 0.35)
    # pads, + and - marks
    for chain, col in ((white, WHITE), (ir, IR)):
        for k, (ref, n) in enumerate(chain):
            x, y = pin(ref, n)
            d.ellipse((x - 11, y - 11, x + 11, y + 11), fill=col, outline=(0, 0, 0), width=2)
            sign = "+" if (ref in ("J1", "J2") and n == "1") or (ref not in ("J1", "J2") and k % 2 == 1) else "-"
            d.text((x + 13, y - 15), sign, fill=col, font=f, stroke_width=3, stroke_fill="white")
    for ref in ("J1", "J2", "J3", "J6", "J7", "J8", "J9", "J10", "J11", "J12"):
        q = [P(*v[:2]) for v in comps[ref]["pins"].values()]
        cx = sum(a for a, _ in q) / len(q)
        cy = min(b for _, b in q)
        d.text((cx, cy - 34), ref, fill=(30, 30, 30), font=fs, anchor="mm", stroke_width=3, stroke_fill="white")
    for name, x in (("A", 2.5), ("B", 8.5), ("C", 14.5), ("D", 20.5)):
        d.text(P(x, y1 - 1.2), name, fill=(40, 90, 40), font=f, anchor="mm")
    d.text((W / 2, 36), "LED wiring, top side: white J1 -> A -> B -> C -> D -> J1, IR J2 -> A -> B -> C -> D -> J2",
           fill=(0, 0, 0), font=fs, anchor="mm")
    img.save(out)
    print("wrote", out)


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
    draw_led_wiring(os.path.join(HERE, "led_wiring.png"))
    draw_led_wiring_board(pcb, comps, os.path.join(HERE, "led_wiring_board.png"))
    draw_pr1(pcb, comps, os.path.join(HERE, "pr1_wiring.png"))
    hookup = json.load(open(os.path.join(HERE, "testpoints.json")))["hookup"]
    for side in ("top", "bottom"):
        render_hookup(pcb, comps, hookup, side, os.path.join(HERE, f"hookup_{side}.png"))


if __name__ == "__main__":
    main()
