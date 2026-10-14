#!/usr/bin/env python3
"""check_screws.py PARTS_DIR [LEGS_DIR]: every screw hole of the built LEAKCAM parts has >= 1.0 mm of
material all round it over its length and past a blind hole's tip, open only where the screw goes in (and
comes out if it passes right through). PARTS_DIR holds base_sw.step, cover_sw.step, cradle_sw.step (SolidWorks exports, base
frame mm); LEGS_DIR (default: this folder) form_model.py's led_poses.json and led_clamp_<i>.step.

For each hole: rings of points at radius r + 1.0 round its axis, every 0.5 mm along it, the first
(r + 1.0) x 1.5 mm after the entry left out (the mouth on a sloped face); a point that is not inside
the part's material is a breakout. Prints each hole and the breakouts found; exit 1 if any.
"""
import json, math, os, sys
import numpy as np
import build123d as b

HERE = os.path.dirname(os.path.abspath(__file__))
WALL = 1.0

# the screws (base frame mm): name, part(s), axis entry point, axis direction (into the material),
# sections [(from, to, radius)] in mm along the axis from the entry point, blind end or through
SEAM = [(x, z) for z in (-37.0, 35.8) for x in (-11.8, 11.8)]          # clips.py SEAM_SCREWS
CAM = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]     # camera holes (X, Z)
BOARD_HOLE = (-15.64, -6.64)


def holes(legs_dir):
    out = []
    for x, z in SEAM:                       # from the bottom skin up: counterbore to Y -6, hole to the seam, pilot to 10.7
        out.append((f"seam screw X {x} Z {z}", ("base", "cover"), (x, -40.0, z), (0, 1, 0),
                    [(0.0, 34.0, 1.9), (34.0, 44.7, 1.05), (44.7, 50.7, 0.8)]))
    for x, z in CAM:
        out.append((f"bottom camera screw X {x} Z {z}", ("base",), (x, -7.18, z), (0, -1, 0), [(0.0, 3.5, 0.8)]))
        out.append((f"top camera screw X {x} Z {z}", ("cover",), (x, 22.18, z), (0, 1, 0), [(0.0, 3.5, 0.8)]))
    out.append(("board screw pilot", ("base",), (BOARD_HOLE[0], 0.0, BOARD_HOLE[1]), (0, -1, 0), [(0.0, 3.5, 0.8)]))
    out.append(("cradle board screw", ("cradle",), (BOARD_HOLE[0], 1.6, BOARD_HOLE[1]), (0, 1, 0),
                [(0.0, 3.75, 1.1), (3.75, 5.0, 1.95)], "through"))   # hole from the board up, counterbore 5.35..6.6
    poses = os.path.join(legs_dir, "led_poses.json")
    if os.path.exists(poses):
        P = json.load(open(os.path.join(HERE, "form_params.json")))["leds"]["clamp"]
        for i, p in enumerate(json.load(open(poses)), 1):
            n = np.array(p["screw"][0][0:3]); t = np.array(p["screw"][1])          # shank direction, head top
            top = t + n * 1.1                                                      # the clamp's top
            ear = P["ear_t"]                                                         # the clamp's ear, then gap, then the boss
            out.append((f"LED clamp {i} hole", (f"led_clamp_{i}",), tuple(top), tuple(n), [(0.0, ear, 1.1)], "through"))
            out.append((f"LED boss {i} pilot", (p["part"],), tuple(top + n * (ear + P["gap"])), tuple(n),
                        [(0.0, p["screw_len"] - ear - P["gap"], 0.8)]))
    return out


def main():
    parts_dir = sys.argv[1]
    legs_dir = sys.argv[2] if len(sys.argv) > 2 else HERE
    solids = {}
    def solid(name):
        if name not in solids:
            p = os.path.join(parts_dir, f"{name}_sw.step") if not name.startswith("led_clamp") else os.path.join(legs_dir, f"{name}.step")
            solids[name] = b.import_step(p)
        return solids[name]
    bad = 0
    for name, parts, p0, d, sections, *rest in holes(legs_dir):
        blind = not rest or rest[0] != "through"
        p0, d = np.array(p0, float), np.array(d, float); d /= np.linalg.norm(d)
        u = np.cross(d, [0.3, 0.7, 0.1]); u /= np.linalg.norm(u); v = np.cross(d, u)
        inside = lambda q: any(solid(pn).is_inside(b.Vector(*map(float, q))) for pn in parts)
        # where the axis first enters material: the mouth
        entry = next((s for s in np.arange(0, sections[-1][1], 0.25) if inside(p0 + d * s + u * (sections[0][2] + 0.3))), 0.0)
        found = []
        for s0, s1, r in sections:
            R = r + WALL
            for s in np.arange(max(s0, entry + R * 1.5) + 0.3, s1 - 0.3 + 1e-6, 0.5):
                for a in np.linspace(0, 2 * math.pi, 16, endpoint=False):
                    q = p0 + d * s + R * (math.cos(a) * u + math.sin(a) * v)
                    if not inside(q):
                        found.append((round(float(s), 1), r, np.round(q, 1).tolist()))
        if blind:                                         # past the tip: >= 1.0 mm on the axis and round it
            s_end, r_end = sections[-1][1], sections[-1][2]
            for t in np.arange(0.25, WALL + 0.01, 0.25):
                for rr, angs in ((0.0, [0.0]), (r_end, np.linspace(0, 2 * math.pi, 8, endpoint=False))):
                    for a in angs:
                        q = p0 + d * (s_end + t) + rr * (math.cos(a) * u + math.sin(a) * v)
                        if not inside(q):
                            found.append(("tip", round(float(t), 2), np.round(q, 1).tolist()))
        print(f"{'OK ' if not found else 'BAD'} {name}: {len(found)} breakout points" + (f", e.g. {found[:3]}" if found else ""))
        bad += bool(found)
    print(f"{bad} holes with less than {WALL} mm round them")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
