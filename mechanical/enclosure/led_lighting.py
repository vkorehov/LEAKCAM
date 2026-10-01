#!/usr/bin/env python3
"""How the LED strip tilt spreads the light over one camera's view (the other camera's is the mirror).

  python3 led_lighting.py [--lens-r 8 --lens-h 10] [--png lighting.png]

Geometry from led_strips.py (the chamfer strips of the cover, top camera, lens up +Y). Each LED is
a cos^m emitter (m from its datasheet beam: white 130 deg -> 0.80, IR 120 deg -> 1.0), light only in
front of its strip, shadowed by the lens (a cylinder over the top face; its size is a guess until
the 222 deg lens is measured). Scenes:

  sphere  every direction of the 222 deg view at 1 m: how even the picture is
  floor   a plane 0.5 m in front of the lens, out to 60 deg (0.87 m off the axis)
  glare   the specular hotspot a shiny surface facing the camera (water on a floor) throws back:
          the LED's intensity along the camera axis, relative to an untilted LED
"""
import argparse
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ns = {}
exec(open(os.path.join(HERE, "led_strips.py")).read(), ns)

TOP, OUTER_X, CAM_Z = 29.6, 19.55, -1.1                   # cover top, side faces, camera axis (build_cover.py)
M_WHITE = math.log(0.5) / math.log(math.cos(math.radians(65)))
M_IR = math.log(0.5) / math.log(math.cos(math.radians(60)))
HALF_VIEW = 111.0                                          # 222 deg fisheye


def leds(tilt):
    """(position mm, axis, m) of the eight LEDs of the top camera for a strip tilt"""
    ns["TILT"] = tilt
    out = []
    for side in (-1, 1):
        point, t, n, p1 = ns["strip_frame"](side, 1, TOP, OUTER_X)
        x, y = point(0.0, -0.8)                            # the dome, just outside the face
        for z, kind in zip(ns["LEDS_AT"], ("W", "IR", "IR", "W")):
            out.append((np.array([x, y, ns["STRIP_Z0"] + z]), np.array([n[0], n[1], 0.0]),
                        M_WHITE if kind == "W" else M_IR))
    return out


def shadowed(p, d, lens_r, lens_h):
    """does the ray from p along d hit the lens cylinder (axis X 0, Z CAM_Z, Y TOP..TOP+lens_h)?"""
    ox, oz = p[0], p[2] - CAM_Z
    a = d[:, 0] ** 2 + d[:, 2] ** 2
    b = 2 * (ox * d[:, 0] + oz * d[:, 2])
    c = ox * ox + oz * oz - lens_r ** 2
    disc = b * b - 4 * a * c
    hit = np.zeros(len(d), bool)
    ok = (disc > 0) & (a > 1e-12)
    s = (-b[ok] - np.sqrt(disc[ok])) / (2 * a[ok])
    y = p[1] + s * d[ok, 1]
    hit[ok] = (s > 0) & (y > TOP) & (y < TOP + lens_h)
    return hit


def irradiance(targets, normals, tilt, lens_r, lens_h):
    e = np.zeros(len(targets))
    for p, axis, m in leds(tilt):
        v = targets - p
        r = np.linalg.norm(v, axis=1)
        d = v / r[:, None]
        cos_out = d @ axis
        lit = (cos_out > 0) & ~shadowed(p, d, lens_r, lens_h)
        cos_in = np.clip(-(d * normals).sum(axis=1), 0, None)
        e += np.where(lit, np.clip(cos_out, 0, None) ** m * cos_in / (r / 1000) ** 2, 0)
    return e


def view_dirs(max_deg, n=90):
    th = np.radians(np.linspace(0, max_deg, n))
    ph = np.radians(np.linspace(0, 360, 4 * n, endpoint=False))
    T, P = np.meshgrid(th, ph, indexing="ij")
    d = np.stack([np.sin(T) * np.cos(P), np.cos(T), np.sin(T) * np.sin(P)], axis=-1)
    return T, d, np.sin(T)                                 # solid angle weight


def stats(e, w):
    mean = (e * w).sum() / w.sum()
    return e.min() / e.max(), np.sqrt(((e - mean) ** 2 * w).sum() / w.sum()) / mean


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lens-r", type=float, default=8.0)
    ap.add_argument("--lens-h", type=float, default=10.0)
    ap.add_argument("--png")
    a = ap.parse_args()
    cam = np.array([0.0, TOP, CAM_Z])
    tilts = [0, 10, 15, 20, 25, 30, 35, 40, 45, 60]
    T, d, w = view_dirs(HALF_VIEW)
    Tf, df, wf = view_dirs(60)
    main_ = np.degrees(T[:, 0]) <= 90
    print(f"lens assumed: radius {a.lens_r} mm, {a.lens_h} mm above the top face")
    print("       sphere at 1 m, view to 90 deg      edge band 90-111 deg   floor 0.5 m, to 60 deg   specular")
    print(" tilt  min/max   CV   axis/90deg           lit    mean/axis       min/max   CV             hotspot")
    curves = {}
    for tilt in tilts:
        tg = cam + 1000 * d.reshape(-1, 3)
        e = irradiance(tg, -d.reshape(-1, 3), tilt, a.lens_r, a.lens_h).reshape(T.shape)
        mm, cv = stats(e[main_], np.broadcast_to(w, e.shape)[main_])
        prof = e.mean(axis=1)
        i90 = np.argmin(abs(np.degrees(T[:, 0]) - 90))
        band = e[~main_]
        lit = (band > 1e-6 * e.max()).mean()
        s = 500 / np.cos(Tf)                               # floor 0.5 m in front of the lens
        tgf = cam + (s[..., None] * df).reshape(-1, 3)
        ef = irradiance(tgf, np.tile([0.0, -1.0, 0.0], (tgf.shape[0], 1)), tilt, a.lens_r, a.lens_h).reshape(Tf.shape)
        fmm, fcv = stats(ef, np.broadcast_to(wf, ef.shape))
        glare = np.mean([max(axis[1], 0) ** m for p, axis, m in leds(tilt)])
        print(f" {tilt:3d}   {mm:6.3f}  {cv:5.2f}  {prof[0] / prof[i90]:7.1f}           {lit:5.0%}  {band.mean() / prof[0]:8.3f}"
              f"        {fmm:6.3f}  {fcv:5.2f}          {glare:5.2f}")
        curves[tilt] = (np.degrees(T[:, 0]), prof / prof.max(), e.min(axis=1) / prof.max())
    if a.png:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(9, 5))
        for tilt in (0, 15, 20, 30, 45, 60):
            th, mean, lo = curves[tilt]
            ax.plot(th, mean, label=f"{tilt} deg")
            ax.plot(th, lo, ls=":", color=ax.lines[-1].get_color())
        ax.axvline(90, color="grey", lw=0.5)
        ax.set_xlabel("angle from the camera axis (deg); 111 = edge of the 222 deg view")
        ax.set_ylabel("irradiance at 1 m (solid: mean over azimuth, dotted: darkest azimuth)")
        ax.set_title("LED strip tilt: light across one camera's view")
        ax.legend(title="strip tilt")
        ax.grid(alpha=0.3)
        fig.tight_layout()
        fig.savefig(a.png, dpi=110)
        print("plot:", a.png)


if __name__ == "__main__":
    main()
