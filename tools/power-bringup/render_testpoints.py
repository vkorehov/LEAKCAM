#!/usr/bin/env python3
"""Draw the power bring-up probe points on the MIFA board, from the fab ODB++.

  python3 render_testpoints.py [MIFA.tgz | git revision]   -> top.png, top_power_zoom.png,
      hookup_top.png, hookup_bottom.png next to this script

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


def render(pcb, comps, points, out, region=None, px=PX, label=""):
    """the probe points on the top side (all of them are there)"""
    pts = outline(pcb)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    x0, x1, y0, y1 = region or (min(xs), max(xs), min(ys), max(ys))
    PX = px
    W = int((x1 - x0) * PX) + 2 * MARGIN
    H = int((y1 - y0) * PX) + 160

    def P(x, y):
        return MARGIN + (x - x0) * PX, 110 + (y1 - y) * PX

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
    copper, silk = "top_layer", "top_overlay"
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
        if not cp or cp["side"] != "top":
            continue
        if not all(inside(x, y) for x, y, _ in cp["pins"].values()):
            continue
        px = [P(x, y) for x, y, _ in cp["pins"].values()]
        cx, cy = sum(p[0] for p in px) / len(px), sum(p[1] for p in px) / len(px)
        d.text((cx, cy), ref, fill=(30, 30, 160), font=font, anchor="mm", stroke_width=3, stroke_fill="white")
    # probe points: a ring on the rail pad, a small ring on its GND pad, a numbered label at the
    # side nearer to the pad, at the pad's height (labels pushed apart so they do not overlap)
    pos = lambda key, p: P(*comps[p[key].split(".")[0]]["pins"][p[key].split(".")[1]][:2])
    mine = [p for p in points if p["side"] == "top" and inside(*comps[p["pad"].split(".")[0]]["pins"][p["pad"].split(".")[1]][:2])]
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
    title = f"MIFA rev 1 power bring-up, top side{label}"
    d.text((W / 2, 30), title, fill=(0, 0, 0), font=big, anchor="mm")
    d.text((W / 2, 64), "red ring = probe (+), black ring = GND for the black lead; numbers match testpoints.json and power_check.py",
           fill=(60, 60, 60), font=small, anchor="mm")
    img.save(out)
    print("wrote", out, f"({len(mine)} probe points)")


def render_hookup(pcb, comps, side, out):
    """the hand-wired parts drawn and wired to their real pads: battery, NTC, LED strips (top);
    USB cable on PR1, leak probes (bottom); camera ribbons and the B1 short; short names only"""
    pts = outline(pcb)
    bx0, bx1 = min(p[0] for p in pts), max(p[0] for p in pts)
    by0, by1 = min(p[1] for p in pts), max(p[1] for p in pts)
    flip = side == "bottom"
    S = 24.0
    LM, RM, TM, BM = 380, 520, 120, 860
    W = int((bx1 - bx0) * S) + LM + RM
    H = int((by1 - by0) * S) + TM + BM

    def P(x, y):
        u = (bx1 - x) if flip else (x - bx0)
        return LM + u * S, TM + (by1 - y) * S

    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype("DejaVuSans-Bold.ttf", 20)
        fl = ImageFont.truetype("DejaVuSans-Bold.ttf", 26)
    except OSError:
        f = fl = ImageFont.load_default()
    # the board as fabricated: outline, copper pads, silkscreen
    d.polygon([P(x, y) for x, y in pts], fill=(232, 240, 228), outline=(40, 90, 40))
    pads, _ = features(pcb, "top_layer" if side == "top" else "bottom_layer")
    for x, y, shape, w, h, ang in pads:
        if shape == "r":
            cx, cy = P(x, y)
            r = w / 2 * S
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(205, 170, 120))
            continue
        a = math.radians(ang)
        c, s_ = math.cos(a), math.sin(a)
        d.polygon([P(x + dx * c + dy * s_, y - dx * s_ + dy * c) for dx, dy in
                   ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2))], fill=(205, 170, 120))
    _, sl = features(pcb, "top_overlay" if side == "top" else "bottom_overlay")
    for xa, ya, xb, yb, wd in sl:
        d.line((P(xa, ya), P(xb, yb)), fill=(150, 150, 150), width=max(1, int(wd * S)))
    pin = lambda ref, n: P(*comps[ref]["pins"][n][:2])

    def poly(pts_, col, width=6, dash=False):
        if not dash:
            d.line(pts_, fill=col, width=width, joint="curve")
            return
        for (xa, ya), (xb, yb) in zip(pts_, pts_[1:]):
            n = max(1, int(math.hypot(xb - xa, yb - ya) / 16))
            for k in range(0, n, 2):
                d.line((xa + (xb - xa) * k / n, ya + (yb - ya) * k / n,
                        xa + (xb - xa) * min(k + 1, n) / n, ya + (yb - ya) * min(k + 1, n) / n), fill=col, width=width)

    def dot(p, col):
        d.ellipse((p[0] - 9, p[1] - 9, p[0] + 9, p[1] + 9), fill=col, outline=(0, 0, 0), width=2)

    def curve(a, b, col, bend, width=6):
        (ax, ay), (bx_, by_) = a, b
        mx, my = (ax + bx_) / 2, (ay + by_) / 2 + bend
        q = [((1 - t) ** 2 * ax + 2 * (1 - t) * t * mx + t * t * bx_,
              (1 - t) ** 2 * ay + 2 * (1 - t) * t * my + t * t * by_) for t in [k / 40 for k in range(41)]]
        d.line(q, fill=col, width=width, joint="curve")

    def camera(ref, label):
        q = [pin(ref, n) for n in comps[ref]["pins"] if n.isdigit() and int(n) <= 22]
        xa, xb = min(a for a, _ in q), max(a for a, _ in q)
        ytop = max(b for _, b in q)
        cx = (xa + xb) / 2
        cam_w = 25 * S
        cy0 = TM + (by1 - by0) * S + 160
        # ribbon: 22-pin end in the connector, 15-pin end in the camera
        d.polygon([(xa, ytop), (xb, ytop), (cx + 9 * S / 2, cy0), (cx - 9 * S / 2, cy0)], fill=(215, 175, 90), outline=(120, 90, 30))
        for k in range(1, 8):
            t = k / 8
            d.line((xa + (cx - 9 * S / 2 - xa) * 0 + (xb - xa) * t, ytop, cx - 9 * S / 2 + 9 * S * t, cy0), fill=(160, 120, 50), width=1)
        d.rounded_rectangle((cx - cam_w / 2, cy0, cx + cam_w / 2, cy0 + 24 * S), radius=8, fill=(30, 110, 60), outline=(0, 0, 0), width=2)
        d.ellipse((cx - 5.5 * S, cy0 + 12 * S - 5.5 * S, cx + 5.5 * S, cy0 + 12 * S + 5.5 * S), fill=(20, 20, 20), outline=(90, 90, 90), width=3)
        d.text((cx + cam_w / 2 + 16, cy0 + 12 * S), label, fill=(30, 110, 60), font=fl, anchor="lm")

    if side == "top":
        # LED chains on the strips (white J1, IR J2), each strip's - to the next strip's +
        WHITE, IR = (225, 145, 0), (170, 20, 140)
        white = [("J1", "1"), ("J3", "2"), ("J3", "1"), ("J7", "2"), ("J7", "1"), ("J9", "2"), ("J9", "1"),
                 ("J11", "2"), ("J11", "1"), ("J1", "2")]
        ir = [("J2", "1"), ("J6", "1"), ("J6", "2"), ("J8", "1"), ("J8", "2"), ("J10", "1"), ("J10", "2"),
              ("J12", "1"), ("J12", "2"), ("J2", "2")]
        for chain, col, bend in ((white, WHITE, -110), (ir, IR, 110)):
            for k in range(0, len(chain), 2):
                curve(pin(*chain[k]), pin(*chain[k + 1]), col, bend if k in (0, len(chain) - 2) else bend * 0.3)
            for k, (ref, n) in enumerate(chain):
                p = pin(ref, n)
                dot(p, col)
                plus = (ref in ("J1", "J2") and n == "1") or (ref not in ("J1", "J2") and k % 2 == 1)
                d.text((p[0] + 10, p[1] - 26), "+" if plus else "-", fill=col, font=fl, stroke_width=3, stroke_fill="white")
        for name, x in (("A", 2.5), ("B", 8.5), ("C", 14.5), ("D", 20.5)):
            d.text((P(x, 0)[0], TM - 30), name, fill=(40, 90, 40), font=fl, anchor="mm")
        d.text((LM - 20, P(0, 45.6)[1] - 40), "white", fill=WHITE, font=fl, anchor="rm")
        d.text((LM - 20, P(0, 44.0)[1] + 40), "IR", fill=IR, font=fl, anchor="rm")
        # battery cells in the right margin: red + to pin 1 (BAT), black - to pin 2 (GND); the cell's
        # terminals are drawn in the same top-to-bottom order as the pads, so the leads never cross
        bx = W - RM + 140
        for ref, y_mm, dash, label in (("JB1", 37.0, False, "LiPo"), ("JB2", 22.0, True, "2nd LiPo (option)")):
            p1, p2 = pin(ref, "1"), pin(ref, "2")
            top = P(0, y_mm)[1]
            box = (bx, top, bx + 300, top + 170)
            if dash:
                poly([(box[0], box[1]), (box[2], box[1]), (box[2], box[3]), (box[0], box[3]), (box[0], box[1])], (90, 110, 150), 3, True)
            else:
                d.rounded_rectangle(box, radius=14, fill=(190, 205, 225), outline=(60, 80, 120), width=3)
            d.text(((box[0] + box[2]) / 2, box[3] - 40), label, fill=(40, 60, 100), font=fl, anchor="mm")
            upper, lower = (p1, p2) if p1[1] < p2[1] else (p2, p1)
            for k, q in enumerate((upper, lower)):
                is_plus = q == p1
                col = (220, 0, 0) if is_plus else (0, 0, 0)
                ty = box[1] + 35 + k * 40
                xr = bx - 60 - k * 30
                poly([q, (xr, q[1]), (xr, ty), (box[0], ty)], col, 6, dash)
                d.text((box[0] + 14, ty), "+" if is_plus else "-", fill=col, font=fl, anchor="lm")
                dot(q, col)
        # NTC bead glued to the first cell, two leads to JT (no polarity)
        nt = (bx + 240, P(0, 37.0)[1] + 170)
        for n, off in (("1", -6), ("2", 6)):
            q = pin("JT", n)
            poly([q, (q[0] + 30 + off, q[1]), (nt[0] + off, nt[1] + 60), (nt[0] + off, nt[1] + 8)], (90, 90, 90), 4)
            dot(q, (90, 90, 90))
        d.ellipse((nt[0] - 12, nt[1] - 12, nt[0] + 12, nt[1] + 12), fill=(20, 20, 20))
        d.text((nt[0] + 24, nt[1] + 40), "NTC", fill=(60, 60, 60), font=fl, anchor="lm")
        # B1: short for K230 USB boot (only when needed)
        a, b = pin("B1_P", "1"), pin("B1_N", "1")
        poly([a, (a[0] - 40, a[1]), (b[0] - 40, b[1]), b], (200, 0, 0), 5, True)
        dot(a, (200, 0, 0)); dot(b, (200, 0, 0))
        d.text((a[0] - 50, (a[1] + b[1]) / 2), "B1 short:\nK230 USB boot", fill=(200, 0, 0), font=f, anchor="rm")
        camera("J5", "camera 1")
    else:
        # USB cable on PR1: green D+ 1, white D- 2, black GND 3, each straight out at its pad's height;
        # red VBUS cut back and taped; 4-6 link = flash mode
        ys_ = [pin("PR1", n)[1] for n in "123"]
        ux, uy0, uy1 = W - RM + 220, min(ys_) - 50, max(ys_) + 50
        d.rectangle((ux, uy0, ux + 200, uy1), fill=(240, 240, 240), outline=(0, 0, 0), width=4)
        d.text((ux + 100, (uy0 + uy1) / 2), "USB-A\nto PC", fill=(0, 0, 0), font=fl, anchor="mm")
        for n, col in (("1", (0, 150, 0)), ("2", (160, 160, 160)), ("3", (0, 0, 0))):
            q = pin("PR1", n)
            poly([q, (ux, q[1])], col, 6)
            dot(q, col)
        d.line((ux - 60, uy0 + 12, ux, uy0 + 12), fill=(220, 0, 0), width=6)
        d.rectangle((ux - 72, uy0 + 4, ux - 58, uy0 + 20), fill=(40, 40, 40))
        for n, col in (("4", (0, 90, 200)), ("5", (200, 100, 0)), ("6", (200, 0, 0))):
            dot(pin("PR1", n), col)
        a, b = pin("PR1", "4"), pin("PR1", "6")
        poly([a, (a[0] - 45, a[1]), (b[0] - 45, b[1]), b], (0, 90, 200), 5, True)
        d.text((a[0] - 55, (a[1] + b[1]) / 2), "4-6: flash mode", fill=(0, 90, 200), font=f, anchor="rm")
        for n, lab in zip("123456", ("D+", "D-", "GND", "BOOT", "EN", "3V3")):
            q = pin("PR1", n)
            d.text((q[0] + 14, q[1] - 14), f"{n} {lab}", fill=(0, 0, 0), font=f, anchor="lb", stroke_width=3, stroke_fill="white")
        # leak probes: a 2-wire lead from each connector to a pair of probe pins (no polarity)
        for k, ref in enumerate(("S3", "S2", "S1")):
            q1, q2 = sorted((pin(ref, "1"), pin(ref, "2")), key=lambda q: q[1])   # upper pad to the upper pin
            px_, py_ = 70, (q1[1] + q2[1]) / 2
            for q, off in ((q1, -14), (q2, 14)):
                poly([q, (q[0] - 40, q[1]), (px_ + 60, py_ + off), (px_ + 10, py_ + off)], (30, 80, 200), 5)
                dot(q, (30, 80, 200))
                d.rectangle((px_ - 40, py_ + off - 4, px_ + 10, py_ + off + 4), fill=(170, 170, 170), outline=(80, 80, 80))
            d.text((px_ - 40, py_ - 40), f"probe {ref[1]}", fill=(30, 80, 200), font=fl, anchor="lm")
        camera("J4", "camera 2")
    d.text((W / 2, 40), f"MIFA rev 1, {side} side" + (" (seen from below)" if flip else ""), fill=(0, 0, 0), font=fl, anchor="mm")
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
    render(pcb, comps, points, os.path.join(HERE, "top.png"))
    render(pcb, comps, points, os.path.join(HERE, "top_power_zoom.png"), region=(43.0, 60.5, 24.0, 49.0),
           px=55.0, label=", power corner (zoom)")
    for side in ("top", "bottom"):
        render_hookup(pcb, comps, side, os.path.join(HERE, f"hookup_{side}.png"))


if __name__ == "__main__":
    main()
