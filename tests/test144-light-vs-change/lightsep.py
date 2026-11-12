#!/usr/bin/env python3
"""test144 step 2: lighting changes against real changes, on the host, with the product's comparison (imgdiff.c built
for this machine). Pairs from the real frames (frames/, capture_frames.py): a frame against
  - itself under the other lighting (LED <-> room: a real pair, the same scene),
  - transforms of itself that are LIGHT: global gain, gamma, a smooth gradient, a soft shadow, a hard-edged shadow;
  - transforms that are REAL: an object, a puddle (dark, with the LEDs' specular spots), a wet-floor darkening (hard
    edge, the texture kept), a thin water streak.
Each pair gets imgdiff's verdict (changed / same) and the candidate light feature: in imgdiff's changed blocks, the
texture's correlation after each block's own mean and contrast are taken out (a lighting change keeps the texture:
high; an object or water replaces it: low). The table is the confusion matrix per class.

Pass (now): every REAL pair changed (no missed leak) and every SAME pair same. The LIGHT classes are reported, not
required: imgdiff alone cannot tell a gradient or a shadow from water; they are the target of the change net (KPU,
DESIGN.md 5.3.12), which must keep REAL at 100 % and bring LIGHT false alarms to 0 on this same set.

    tests/test144-light-vs-change/lightsep.py [--save]     --save: the pair images into pairs/ for a look
"""
import ctypes
import glob
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "firmware", "k230_app")
W, H, BLOCK = 2592, 1936, 16                 # imgdiff.h: the native frame, 16x16 blocks
BX, BY, S = W // BLOCK, H // BLOCK, W / 320  # S: the transforms were drawn on 320x240
SAVE = "--save" in sys.argv


class Cfg(ctypes.Structure):
    _fields_ = [("circle_radius", ctypes.c_float), ("pixel_thresh", ctypes.c_int), ("block_thresh", ctypes.c_float),
                ("block_pixels", ctypes.c_int), ("frac_thresh", ctypes.c_float), ("dark_floor", ctypes.c_int)]


class Res(ctypes.Structure):
    _fields_ = [("changed", ctypes.c_bool), ("changed_blocks", ctypes.c_int), ("max_block_mad", ctypes.c_float),
                ("changed_frac", ctypes.c_float), ("mean_ref", ctypes.c_float), ("mean_new", ctypes.c_float),
                ("gain", ctypes.c_float), ("usable_blocks", ctypes.c_int),
                ("block_changed", (ctypes.c_uint8 * BX) * BY)]


def lib():
    so = os.path.join(tempfile.mkdtemp(), "imgdiff.so")
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-I", SRC, os.path.join(SRC, "imgdiff.c"), "-lm", "-o", so],
                   check=True)
    lb = ctypes.CDLL(so)
    lb.imgdiff_compare.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Cfg),
                                   ctypes.POINTER(Res)]
    return lb


LIB = lib()
CFG = Cfg()
LIB.imgdiff_default_cfg(ctypes.byref(CFG))


def luma(path):
    """the job's input: the camera's native luma (the JPEG's Y, 2592x1936), compared as it is (no reduction)"""
    y = np.asarray(Image.open(path).convert("YCbCr"), dtype=np.uint8)[..., 0].copy()
    assert y.shape == (H, W), f"{path}: {y.shape}, not the native {H}x{W}"
    return y


def compare(ref, cur):
    r = Res()
    LIB.imgdiff_compare(ref.tobytes(), cur.tobytes(), W, ctypes.byref(CFG), ctypes.byref(r))
    return r


def texture_kept(ref, cur, r):
    """the candidate light feature: the median correlation of the texture (each block's mean and contrast taken out)
    over imgdiff's changed blocks that have texture in the reference (sd >= TEXTURE_SD: a flat ceiling block's
    "texture" is noise and correlates with nothing, lighting or not); 1.0 when no such block changed"""
    cs = []
    bw, bh = W // BX, H // BY
    for by in range(BY):
        for bx in range(BX):
            if not r.block_changed[by][bx]:
                continue
            a = ref[by * bh:(by + 1) * bh, bx * bw:(bx + 1) * bw].astype(np.float32).ravel()
            b = cur[by * bh:(by + 1) * bh, bx * bw:(bx + 1) * bw].astype(np.float32).ravel()
            if a.std() < TEXTURE_SD:
                continue
            a, b = a - a.mean(), b - b.mean()
            den = float(np.sqrt((a * a).sum() * (b * b).sum()))
            cs.append(float((a * b).sum() / den) if den > 0 else 0.0)
    return float(np.median(cs)) if cs else 1.0


TEXTURE_SD = 6.0


# ---- the transforms (drawn on the 320x240 scale, S times larger on the native frame) ----
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
rng = np.random.default_rng(144)


def clip(a):
    return np.clip(a, 0, 255).astype(np.uint8)


def blur(m, k):
    """a box blur of k pixels on the 320x240 scale (separable), for soft edges"""
    k = max(1, int(k * S))
    ker = np.ones(k, np.float32) / k
    m = np.apply_along_axis(lambda v: np.convolve(v, ker, mode="same"), 1, m)
    return np.apply_along_axis(lambda v: np.convolve(v, ker, mode="same"), 0, m)


def ellipse(cx, cy, rx, ry):
    rx, ry = rx * S, ry * S                                 # radii given on the 320x240 scale
    return (((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1).astype(np.float32)


LIGHT = {
    "gain x0.6": lambda a: clip(a * 0.6),
    "gain x1.4": lambda a: clip(a * 1.4),
    "gamma 0.8": lambda a: clip(255 * (a / 255.0) ** 0.8),
    "gradient 0.6-1.3 (side light)": lambda a: clip(a * (0.6 + 0.7 * xx / W)),
    "gradient radial (lamp)": lambda a: clip(a * (1.3 - 0.6 * np.hypot(xx - W * 0.3, yy - H * 0.3) / W)),
    "soft shadow x0.6": lambda a: clip(a * (1 - 0.4 * blur(ellipse(W * 0.6, H * 0.6, 70, 50), 41))),
    "hard shadow x0.6 (a person, a curtain)": lambda a: clip(a * (1 - 0.4 * (xx > W * 0.55))),
}


def puddle(a):
    m = blur(ellipse(W * 0.5, H * 0.7, 55, 30), 5)
    out = a * (1 - 0.55 * m)                                # wet: darker
    for _ in range(6):                                      # the LEDs' reflections on the water
        cx, cy = W * 0.5 + rng.uniform(-40, 40) * S, H * 0.7 + rng.uniform(-18, 18) * S
        out = np.maximum(out, 250 * blur(ellipse(cx, cy, 3, 2), 3))
    return clip(out)


def obj(a):
    out = a.astype(np.float32).copy()
    patch = rng.uniform(30, 220, (40, 60)).astype(np.float32)          # a 40x60 box on the 320x240 scale
    patch = np.kron(blur(patch, 3 / S), np.ones((int(S), int(S)), np.float32))
    y0, x0 = int(150 * S), int(200 * S)
    out[y0:y0 + patch.shape[0], x0:x0 + patch.shape[1]] = patch
    return clip(out)


REAL = {
    "object (a box)": obj,
    "puddle with reflections": puddle,
    "wet floor darker x0.7, hard edge": lambda a: clip(a * (1 - 0.3 * ellipse(W * 0.35, H * 0.65, 80, 45))),
    # the case native resolution is for (user 2026-11-12: "low resolution will not see any leak"): a 10 cm puddle 3 m
    # away, ~17 x 10 native pixels (0.11 deg per pixel), 40 % darker; at 320x240 it was ~2 pixels
    "small puddle 10 cm at 3 m (17x10 px)": lambda a: clip(a * (1 - 0.4 * ellipse(W * 0.45, H * 0.6, 8.5 / S, 5 / S))),
    "water streak": lambda a: clip(np.where((np.abs(xx - (W * 0.4 + 0.3 * (yy - H / 2))) < 3 * S) & (yy > H * 0.3),
                                            a * 0.5, a)),
}


def main():
    frames = sorted(glob.glob(os.path.join(HERE, "frames", "*.jpg")))
    if not frames:
        sys.exit("no frames: capture_frames.py first")
    rows, fails = [], []
    if SAVE:
        os.makedirs(os.path.join(HERE, "pairs"), exist_ok=True)

    def record(group, cls, name, ref, cur):
        r = compare(ref, cur)
        t = texture_kept(ref, cur, r)
        rows.append((group, cls, name, r.changed, r.changed_blocks, r.max_block_mad, t))
        if (group == "REAL" and not r.changed) or (group == "SAME" and r.changed):
            fails.append(f"{group} {cls} {name}: {'changed' if r.changed else 'same'}")
        if SAVE:
            Image.fromarray(np.hstack([ref, cur])).save(os.path.join(HERE, "pairs", f"{group}_{cls}_{name}.png"
                                                                     .replace(" ", "_").replace("/", "-")))

    base = {os.path.basename(f)[:-4]: luma(f) for f in frames}
    for name, a in base.items():
        record("SAME", "itself", name, a, a.copy())
        noisy = clip(a + rng.normal(0, 3, a.shape))          # sensor noise at gain ~4
        record("SAME", "noise sd 3", name, a, noisy)
        for cls, f in LIGHT.items():
            record("LIGHT", cls, name, a, f(a.astype(np.float32)))
        for cls, f in REAL.items():
            record("REAL", cls, name, a, f(a.astype(np.float32)))
    for name, a in base.items():                             # the real pairs: the same scene, LED <-> room
        if "_led_" in name and name.replace("_led_", "_room_") in base:
            record("LIGHT", "LED -> room light (real)", name, a, base[name.replace("_led_", "_room_")])

    print(f"{'group':6} {'class':40} {'pairs':>5} {'changed':>8} {'blocks':>7} {'texture kept (median corr)':>27}")
    for group in ("SAME", "LIGHT", "REAL"):
        for cls in dict.fromkeys(r[1] for r in rows if r[0] == group):
            rs = [r for r in rows if r[0] == group and r[1] == cls]
            ch = sum(r[3] for r in rs)
            blocks = f"{min(r[4] for r in rs)}-{max(r[4] for r in rs)}"
            tk = [r[6] for r in rs if r[3]]
            tks = f"{min(tk):.2f}-{max(tk):.2f}" if tk else "-"
            print(f"{group:6} {cls[:40]:40} {len(rs):5} {ch:5}/{len(rs):<2} {blocks:>7} {tks:>27}")
    light = [r for r in rows if r[0] == "LIGHT"]
    real = [r for r in rows if r[0] == "REAL"]
    print(f"\nimgdiff: REAL detected {sum(r[3] for r in real)}/{len(real)}, LIGHT false alarms "
          f"{sum(r[3] for r in light)}/{len(light)}")
    # the candidate feature on the pairs imgdiff called changed: the best threshold and what it would give
    lc = sorted(r[6] for r in light if r[3])
    rc = sorted(r[6] for r in real if r[3])
    if lc and rc:
        best = max(((t, sum(x >= t for x in lc) + sum(x < t for x in rc)) for t in np.arange(0.3, 1.0, 0.01)),
                   key=lambda p: (p[1], -p[0]))
        t = best[0]
        print(f"candidate 'texture kept' >= {t:.2f} -> light: LIGHT false alarms left "
              f"{sum(x < t for x in lc)}/{len(light)}, REAL missed {sum(x >= t for x in rc)}/{len(real)} "
              f"(fitted on this set: a hint for the change net, not a product threshold)")
    print("summary:", "PASS" if not fails else "FAIL: " + "; ".join(fails))
    sys.exit(1 if fails else 0)


main()
