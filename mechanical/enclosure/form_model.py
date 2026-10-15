#!/usr/bin/env python3
"""form_model.py: the LEAKCAM enclosure's outer form as a parametric model (build123d / OpenCascade).

  form_model.py form_params.json [--seed N] [--out DIR]

Reads the parameters (form_params.json next to this script), writes DIR/form_outer.step (the form,
which build_base.py / build_cover.py start from in SolidWorks), DIR/form_concept.step (with the lenses),
DIR/shell_{base,cover}.step (the parts' hollows, see shell below) and DIR/form_report.json. --seed N replaces the face angles with random starting values (written to the
report, to be copied into the parameters and adjusted by hand).

The form: an oblique body round the device. Its axis runs along the length, skewed off Z by
axis_deg; four long faces (top, bottom, the two sides), each turned by its own roll (about the axis)
and pitch (leaning along it) and set offset_mm outside the interior; two end planes, each with its
own two tilts. With no two faces parallel the body tapers and twists. Each lens sits in a crater:
crater.facets planes falling at crater.slope_deg from its front glass rim (21.5 keeps the 222 deg
fisheye's view clear), the top and bottom faces reaching the lens bodies' ends so the lenses are
buried to the glass.

No plane comes within 1 mm of the interior keep-outs KEEP (a plane that would is pushed out, and the
report says by how much). The form is frozen: a change here must leave form_outer.step identical.

shell: each part is the 2 mm skin (its inner surface follows the outer) plus a band along the parting and
a few simple structures (keep_base / keep_cover: the camera blocks, the board floor and pocket walls, the
plate over the cells, screw bosses, USB-C blocks) and the legs' grips; everything else inside the skin is
hollow (ribs where the user names them).

side_folds: the existing side face folded like paper: its corners and a few sunk points triangulated into flat triangles.

Switch for later (off by default): grooves along the axis.

symmetry "point": only the bottom half is designed (bottom face, -X side, camera end, bottom crater:
"masters"); each plane's twin is its reflection through the midpoint between the lens ends, so the
cover mirrors the base point-wise (not by a plane) - order in the disorder.
"""
import argparse, json, math, os
import numpy as np
import build123d as b
from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
from OCP.TopLoc import TopLoc_Location

HERE = os.path.dirname(os.path.abspath(__file__))
X0, X1, YB, YS, YT, Z0, Z1 = -19.55, 19.55, -14.2, 4.7, 29.9, -39.5, 37.3      # the old block (base | cover at YS)
BIG, MARGIN = 400.0, 1.0
LENS_Z, LENS_TOP_END, LENS_BOTTOM_END = -1.1, 37.9, -22.2                        # lens bodies' outer ends
GLASS_RIM = {1: 38.9, -1: -23.2}                                                  # front glass rim heights (estimated lens)
# what is really inside, the cameras at their own heights (bottom board back Y -4.8, top 19.8 on the cells):
KEEP = [(-18.45, 18.45, 4.7, 18.8, -33.6, 21.7),       # cells and cradle on the board
        (-12.8, 12.8, 18.6, 27.4, -17.2, 7.3),         # top camera: board, holder, the cover's 5 mm plate round its pilots
        (-9.1, 8.4, 4.7, 21.6, -37.5, -30.0),          # J4 flex riser: low at the end,
        (-9.1, 8.4, 4.7, 23.4, -30.0, -24.0),          # up the ramp
        (-9.1, 8.4, 4.7, 24.9, -24.0, -16.6),          # to the top camera's connector (Y 24.5)
        (-13.6, 13.6, 4.7, 11.7, -38.8, -35.2), (-13.6, 13.6, 4.7, 11.7, 34.0, 37.6),   # seam screw pilots (1 mm past)
        (-18.65, 18.65, -4.0, 4.7, -32.8, 32.8),       # main board (36.9 as made), parts down to Y -3.25
        (-19.1, 19.1, 1.5, 4.7, -34.25, 33.45),        # the seam's lip
        (-19.55, 19.55, -5.3, 1.5, -21.0, -5.5),       # USB-C receptacles (to Y -5.0), mouths 0.8 in the flat sides
        (-12.8, 12.8, -12.4, -4.0, -17.2, 7.3),        # bottom camera: board, holder, 5 mm floor round its pilots
        (-9.1, 8.4, -9.5, 4.7, -37.5, -16.6),          # bottom camera flex: along the floor, 180 degree loop into J5
        (-13.6, 13.6, -7.2, 4.7, -38.8, -35.2), (-13.6, 13.6, -7.2, 4.7, 34.0, 37.6)]   # seam screws: head seat Y -6.0
CORNERS = np.array([(x, y, z) for x0, x1, y0, y1, z0, z1 in KEEP for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def rot(v, axis, deg):
    axis = unit(axis); a = math.radians(deg)
    return v * math.cos(a) + np.cross(axis, v) * math.sin(a) + axis * axis.dot(v) * (1 - math.cos(a))


def halfspace(p, n):
    """everything behind the plane through p with outward normal n"""
    pl = b.Plane(origin=tuple(map(float, p)), z_dir=tuple(map(float, unit(n))))
    return pl.location * b.Box(BIG, BIG, BIG, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX))


def outside_keep(p, n):
    """push the plane out until it is MARGIN off every keep-out; returns (point, lift)"""
    n = unit(n)
    lift = max(0.0, (CORNERS @ n).max() + MARGIN - p @ n)
    return p + n * lift, lift


def side_fold(form, d, report, planes=()):
    """fold the existing side face of side d["side"] like paper: its corners and the points d["points"]
    ([Z, Y, depth below the face; negative = raised out of it, a convex point]) triangulated into flat triangles; the region between the old face and
    the folded surface is removed. A point that brings a triangle within MARGIN of the interior is made
    shallower (only that point). The layout is checked: fold lines >= d["min_edge_mm"], triangles <=
    d["max_triangles"] (report["folds"][-1]["ok"])."""
    from scipy.spatial import Delaunay, ConvexHull
    s = d["side"]
    face = max((f for f in form.faces() if f.normal_at().X * s > 0.6), key=lambda f: f.area)
    nv = face.normal_at(); n = unit([nv.X, nv.Y, nv.Z])
    verts = [np.array([v.X, v.Y, v.Z]) for v in face.outer_wire().vertices()]
    o = verts[0]
    ez = unit(np.array([0, 0, 1.0]) - n * n[2]); ey = unit(np.cross(n, ez))
    def on_face(z, y):
        return np.array([o[0] - (n[1] * (y - o[1]) + n[2] * (z - o[2])) / n[0], y, z])
    nv_ = len(verts)
    outline = {tuple(sorted((i, (i + 1) % nv_))) for i in range(nv_)}
    def convex(points):
        h = ConvexHull(np.array(points))
        c = b.Box(BIG, BIG, BIG)
        for eq in h.equations:
            m = eq[:3]; c = c & halfspace(-eq[3] * m, m)
        return c
    footprint = convex([v + n * 12 for v in verts] + [v - n * 12 for v in verts])   # nothing past the face's outline
    depths = [p[2] for p in d["points"]]
    def build():
        pts3 = verts + [on_face(z, y) - n * dep for (z, y, _), dep in zip(d["points"], depths)]
        uv = np.array([[(q - o) @ ez, (q - o) @ ey] for q in pts3])
        return pts3, Delaunay(uv).simplices
    for _ in range(25):
        pts3, tris = build()
        hits = set()
        cut = None
        for t in tris:
            a3 = [pts3[i] for i in t]
            piece = convex(a3 + [q + n * 4 for q in a3]) & footprint
            for x0, x1, y0, y1, z0, z1 in KEEP:
                bx = b.Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * b.Box(x1 - x0 + 2 * MARGIN, y1 - y0 + 2 * MARGIN, z1 - z0 + 2 * MARGIN)
                hit = bx & piece
                if len(hit.solids()) and hit.volume > 0.05:
                    hits |= {i - nv_ for i in t if i >= nv_}
                    break
            cut = piece if cut is None else cut + piece
        if not hits:
            break
        for k in hits:
            depths[k] = depths[k] * 0.85 if depths[k] > 0 else depths[k]
    fold_edges = {tuple(sorted((t[a], t[b_]))) for t in tris for a, b_ in ((0, 1), (1, 2), (0, 2))} - outline
    shortest = min(np.linalg.norm(pts3[i] - pts3[j]) for i, j in fold_edges)
    ok = shortest >= d.get("min_edge_mm", 10) and len(tris) <= d.get("max_triangles", 99)
    report.setdefault("folds", []).append({"side": s, "triangles": len(tris), "shortest_fold_mm": round(shortest, 1),
                                           "depths": [round(x, 2) for x in depths], "ok": bool(ok)})
    form = form - cut
    if any(x < 0 for x in depths):                                       # raised (convex) points: fill under them
        fill = None
        for t in tris:
            a3 = [pts3[i] for i in t]
            base_ = [q - n * (((q - o) @ n) + 1.0) for q in a3]           # down to 1 mm under the old face
            piece = convex(a3 + base_) & footprint
            fill = piece if fill is None else fill + piece
        for p_, m_ in planes:                                           # only outwards from this side: inside every other face
            if unit(m_) @ n < 0.95:
                fill = fill & halfspace(p_, m_)
        form = (form + fill).clean()                                 # merge the coplanar seams
    return form


def form_solid(P, report):
    global GLASS_RIM, LENS_TOP_END, LENS_BOTTOM_END
    up = P.get("lens_raise_mm", 0.0)                                 # lenses moved out (an M12 spacer ring under each)
    GLASS_RIM = {1: 38.9 + up, -1: -23.2 - up}
    LENS_TOP_END, LENS_BOTTOM_END = 37.9 + up, -22.2 - up
    ax = unit([math.tan(math.radians(P["axis_deg"][0])), math.tan(math.radians(P["axis_deg"][1])), 1.0])
    y_perp = unit(np.array([0, 1.0, 0]) - ax * ax[1])
    x_perp = unit(np.cross(y_perp, ax))
    base_dirs = {"top": y_perp, "bottom": -y_perp, "side_px": x_perp, "side_nx": -x_perp}
    planes = []
    for name, f in P["faces"].items():
        n = rot(rot(base_dirs[name], ax, f["roll_deg"]), np.cross(ax, base_dirs[name]), f["pitch_deg"])
        p = n * (CORNERS @ n).max() + n * (MARGIN + f["offset_mm"])
        if name in ("top", "bottom") and P["bury_lenses"]:                  # reach the lens body's end
            end = np.array([0.0, LENS_TOP_END if name == "top" else LENS_BOTTOM_END, LENS_Z])
            if (end - p) @ n > 0:
                p = p + n * ((end - p) @ n)
        planes.append((p, n))
    for name, e in P["ends"].items():
        sgn = 1 if name == "antenna" else -1
        n = unit(rot(rot(sgn * ax, y_perp, e["yaw_deg"]), x_perp, e["pitch_deg"]))
        p = n * (CORNERS @ n).max() + n * (MARGIN + e["offset_mm"])
        planes.append((p, n))
    # craters: planes falling at slope_deg from each lens's front glass rim
    k = math.tan(math.radians(P["crater"]["slope_deg"]))
    craters = []
    for v, rot0 in ((1, P["crater"]["top_rotation_deg"]), (-1, P["crater"]["bottom_rotation_deg"])):
        nf = P["crater"]["facets"]
        az = [math.radians(rot0 + i * 360 / nf + P["crater"].get("jitter_deg", [0] * nf)[i]) for i in range(nf)]
        o = np.array([0.0, GLASS_RIM[v], LENS_Z])
        rays = [np.array([math.cos(a), 0.0, math.sin(a)]) for a in az]
        for i in range(nf):
            r1, r2 = rays[i], rays[(i + 1) % nf]
            p1, p2 = o + 5.5 * r1, o + 5.5 * r2
            p3 = p1 + 40 * r1 - np.array([0, v * 40 * k, 0])
            n = unit(np.cross(p2 - p1, p3 - p1))
            if n[1] * v < 0:
                n = -n
            craters.append((v, p1 - n * P["crater"].get("led_drop_mm", 0.0), n))   # lowered by the LED strips' height
    if P.get("symmetry") == "point":
        # the bottom side is the master: every plane of the bottom half (bottom face, the -X side, the camera
        # end, the bottom crater) and its twin reflected through the midpoint between the lens ends
        c = np.array(P.get("centre", [0.0, (LENS_TOP_END + LENS_BOTTOM_END) / 2, LENS_Z]))
        master = [planes[i] for i, nm in enumerate(list(P["faces"]) + list(P["ends"]))
                  if nm in P.get("masters", ["bottom", "side_nx", "camera"])]
        master += [(p, n) for v, p, n in craters if v < 0]
        planes, report["pair_lift_mm"] = [], []
        for p, n in master:
            lift = max(outside_keep(p, n)[1], outside_keep(2 * c - p, -n)[1])
            report.setdefault("pair_lift_by", []).append([CORNERS[int(np.argmax(CORNERS @ unit(n)))].round(2).tolist(),
                                                          CORNERS[int(np.argmax(CORNERS @ unit(-n)))].round(2).tolist()])
            p = p + unit(n) * lift
            planes += [(p, n), (2 * c - p, -n)]
            report["pair_lift_mm"].append(round(lift, 2))
    else:
        report["crater_lift_mm"] = []
        for v, p1, n in craters:
            p, lift = outside_keep(p1, n)
            report["crater_lift_mm"].append(round(lift, 2))
            planes.append((p, n))
    form = b.Box(BIG, BIG, BIG)
    for p, n in planes:
        form = form & halfspace(p, n)
    for fold in P.get("side_folds", []):
        form = side_fold(form, fold, report, planes)
    return form


def parting(form, cfg):
    """(base, cover, sheet): the form split along a loop of its own edges (cfg["loop"]), every face whole
    in one part. The sheet: flat at cfg["seam_y"] inside the inner outline (the lip, clips and screws stay
    as they are), from there straight to cfg["edge_mm"] inside the loop along the bisector of the two faces
    meeting there (no knife edge on either part), and cfg["past_skin_mm"] out through the skin"""
    loop = np.array(cfg["loop"], float)
    V = [np.array([v.X, v.Y, v.Z]) for v in form.vertices()]
    loop = np.array([min(V, key=lambda q: np.linalg.norm(q - p)) for p in loop])        # exact form vertices
    x0, x1, z0, z1 = cfg["inner"]
    C, YS_ = np.array([0.0, LENS_Z]), cfg["seam_y"]
    rect = np.array([(x0, z0), (x1, z0), (x1, z1), (x0, z1)])
    lxz = loop[:, [0, 2]]

    def ray(theta, poly):                       # first hit of the ray from C on a closed XZ polyline: (t, s, i)
        d = np.array([math.cos(theta), math.sin(theta)]); best = None
        for i in range(len(poly)):
            a, c = poly[i], poly[(i + 1) % len(poly)]
            M_ = np.array([d, a - c]).T
            if abs(np.linalg.det(M_)) < 1e-12:
                continue
            t, s_ = np.linalg.solve(M_, a - C)
            if t > 0 and -1e-9 <= s_ <= 1 + 1e-9 and (best is None or t < best[0]):
                best = (t, s_, i)
        return best

    faces_v = [(np.array([[v.X, v.Y, v.Z] for v in fc.vertices()]), fc) for fc in form.faces()]
    has = lambda vs, p: (np.linalg.norm(vs - p, axis=1) < 0.05).any()
    seg_b = []
    for i in range(len(loop)):
        adj = [fc for vs, fc in faces_v if has(vs, loop[i]) and has(vs, loop[(i + 1) % len(loop)])]
        if len(adj) != 2:
            raise RuntimeError(f"parting loop edge {i}: {len(adj)} faces")
        nb = -sum(np.array([fc.normal_at().X, fc.normal_at().Y, fc.normal_at().Z]) for fc in adj)
        seg_b.append(unit(nb))
    vb = [unit(seg_b[i - 1] + seg_b[i]) for i in range(len(loop))]
    la = [math.atan2(p[1] - C[1], p[0] - C[0]) for p in lxz]
    snap = {}                                   # an outline corner within 8 deg of a loop vertex: one ray, Q on the corner
    angs = list(la)
    for p in rect:
        ra = math.atan2(p[1] - C[1], p[0] - C[0])
        near = min(range(len(la)), key=lambda i: abs((la[i] - ra + math.pi) % (2 * math.pi) - math.pi))
        if abs((la[near] - ra + math.pi) % (2 * math.pi) - math.pi) < math.radians(8):
            snap[la[near]] = p
        else:
            angs.append(ra)
    angs.sort()
    Q, PI, PL, PO = [], [], [], []
    for th in angs:
        d = np.array([math.cos(th), math.sin(th)])
        t, _, _ = ray(th, rect)
        t2, s2, j = ray(th, lxz)
        if t2 <= t:
            raise RuntimeError(f"parting loop inside the inner outline at {math.degrees(th):.1f} deg")
        q, p2 = C + t * d, C + t2 * d
        if th in snap:
            q = np.array(snap[th], float)
        p = np.array([p2[0], loop[j][1] + s2 * (loop[(j + 1) % len(loop)][1] - loop[j][1]), p2[1]])
        bb = unit((1 - s2) * vb[j] + s2 * vb[(j + 1) % len(loop)])
        dep = min(cfg["edge_mm"], 0.5 * (t2 - t) / max(math.hypot(bb[0], bb[2]), 0.2))
        Q.append((q[0], YS_, q[1])); PI.append(p + bb * dep); PL.append(p); PO.append(p - bb * cfg["past_skin_mm"])
    Vv = lambda p: b.Vector(*map(float, p))
    tri = lambda *ps: b.Face(b.Wire.make_polygon([Vv(x) for x in ps], close=True))
    sheet = [b.Face(b.Wire.make_polygon([Vv(q) for q in Q], close=True))]
    n = len(Q)
    for i in range(n):
        j = (i + 1) % n
        for t3 in ((Q[i], Q[j], PI[j]), (Q[i], PI[j], PI[i]), (PI[i], PI[j], PL[j]), (PI[i], PL[j], PL[i]),
                   (PL[i], PL[j], PO[j]), (PL[i], PO[j], PO[i])):            # exactly through the loop
            if np.linalg.norm(np.cross(np.subtract(t3[1], t3[0]), np.subtract(t3[2], t3[0]))) > 1e-9:
                sheet.append(tri(*t3))
    sheet = b.Shell(sheet)
    pieces = form.split(sheet, keep=b.Keep.ALL).solids()
    if len(pieces) != 2:
        raise RuntimeError(f"parting: {len(pieces)} pieces")
    base, cover = sorted(pieces, key=lambda s_: s_.center().Y)
    return base, cover, sheet


def legs(form, cfg, grow=0.0, blocks=()):
    """the probe legs (bendable: sensor wires + a solid spine in heat shrink, glued in). Each tube leaves its
    star (the form vertex nearest pl star: a convex corner, a pyramid of facets) along the pyramid's axis (the
    mean normal of its faces: symmetric to them, user 2026-10-10), glued pl tube_in_mm deep into a round bore. No wire channels
    (the user cuts them by hand): only a plain rectangular pocket round each JST plug's housing.
    grow > 0: the shell's solid block round each bore (the tube's grip), that much bigger. blocks: the kept boxes
    (X0, X1, Y0, Y1, Z0, Z1) a bore must end outside of.
    Returns (base cuts, cover cuts, models for the assembly, info)"""
    V = [np.array([v.X, v.Y, v.Z]) for v in form.vertices()]
    base_cuts, cover_cuts, models, info = [], [], [], []
    tube = cfg["tube_od"] + cfg["fit"]
    def cyl(p0, u, length, dia):
        loc = b.Location(b.Plane(origin=tuple(map(float, p0)), z_dir=tuple(map(float, u))))
        return loc * b.Cylinder(dia / 2, length, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MIN))
    def box(x0, x1, y0, y1, z0, z1):
        x0, x1, y0, y1, z0, z1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1), min(z0, z1), max(z0, z1)
        return b.Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * b.Box(x1 - x0, y1 - y0, z1 - z0)
    for pl in cfg["plugs"]:
        side = 1 if pl["exit"][0] > 0 else -1
        star = min(V, key=lambda q: np.linalg.norm(q - np.array(pl["star"])))
        n0 = unit(sum(np.array(tuple(f.normal_at(f.center()))) for f in form.faces()   # the corner's own axis: the mean
                      if any(np.linalg.norm(np.array([v.X, v.Y, v.Z]) - star) < 1e-3 for v in f.vertices())))   # normal of its faces
        # tilted off it by the least angle that keeps the straight bore (on inwards past the grip, cfg keep_off
        # reach_mm) 1 mm of plastic clear of every screw hole (cfg keep_off axes: vertical screws X, Z, hole radius)
        ko = cfg["keep_off"]
        def seg_ok(a_, e_, end_free=True):                 # a bore from a_ to e_: ends in no kept block, 1 mm of plastic
            r_ = tube / 2                                  # clear of every screw hole
            for k in (blocks if end_free else []):
                if all(k[2 * i] - r_ <= e_[i] <= k[2 * i + 1] + r_ for i in range(3)):
                    return False
            for x_, z_, rh in ko["axes"]:
                d2 = np.array([e_[0] - a_[0], e_[2] - a_[2]]); w_ = np.array([x_ - a_[0], z_ - a_[2]])
                t_ = min(1.0, max(0.0, (w_ @ d2) / max(d2 @ d2, 1e-9)))
                if np.linalg.norm(w_ - t_ * d2) < r_ + rh + ko["wall_mm"]:
                    return False
            return True
        def held_clear(u):                                 # how far the tube can run straight in, clear of the parts
            for t in np.arange(0.0, pl["tube_in_mm"] + 0.01, 0.25):   # inside (cfg keep_off parts: boxes, 0.2 clear)
                q_ = star - u * t
                if any(all(k[2 * i] - tube / 2 - 0.2 <= q_[i] <= k[2 * i + 1] + tube / 2 + 0.2 for i in range(3)) for k in ko["parts"]):
                    return t - 0.25
            return pl["tube_in_mm"]
        clear_ = lambda u: held_clear(u) >= pl["tube_in_mm"] and seg_ok(star + u * 3.0, star - u * (pl["tube_in_mm"] + ko["reach_mm"]))
        p1 = unit(np.cross(n0, [0, 1, 0])); p2 = np.cross(n0, p1)
        n, tilt = n0, 0.0
        for deg in range(0, ko["max_tilt_deg"] + 1):         # (further off looks wrong: then straight on the axis, a shorter glue length)
            ok_ = [u for u in (unit(math.cos(math.radians(deg)) * n0 + math.sin(math.radians(deg)) * (math.cos(az) * p1 + math.sin(az) * p2))
                               for az in np.linspace(0, 2 * math.pi, 24, endpoint=False)) if clear_(u)]
            if ok_:
                n, tilt = max(ok_, key=lambda u: -abs(u[1] - n0[1])), float(deg)   # of equals: the one keeping the corner's tilt
                break
        else:
            tilt = None                                            # none clears: on the corner's axis (reported)
        t_hit = pl["tube_in_mm"] if tilt is not None else max(ko["min_hold_mm"], min(pl["tube_in_mm"], held_clear(n)))
        hit = star - n * t_hit                                             # the bore's inner end
        held = cyl(hit, n, t_hit + 3.0, tube)                              # out through the corner
        gw = cfg["grip_mm"]                                                # the grip's wall round the held length
        ext = [tube / 2 * math.sqrt(max(0.0, 1 - n[i] ** 2)) for i in range(3)]
        thr = min((2 * ext[i] + gw) / abs(n[i]) for i in range(3) if abs(n[i]) > 0.05) + 0.3
        inner = -n                                                         # the way on inwards from the tube's end
        bend = None
        if tilt is None:                           # no straight way clears (user: bend inside the plastic): the tube
            hx0, hx1, hy0, hy1, hz0, hz1 = pl["housing"]                   # straight on the corner's axis, then a bent
            tgt = np.array([(hx1 if side > 0 else hx0), (hy0 + hy1) / 2, (hz0 + hz1) / 2])   # bore (round joint) into its
            if seg_ok(hit, tgt, end_free=False) and unit(tgt - hit) @ (-n) > 0:   # own plug's pocket at the housing's
                bend = unit(tgt - hit); thr = float(np.linalg.norm(tgt - hit))   # rear, clear of the screws
                inner = bend
        tube_bore = cyl(hit, n, t_hit + 3.0, tube)                         # the glued length, out through the corner
        end_slot = None
        if bend is None and pl.get("end_slot_to_y") is not None:           # the tube's end opened downwards (both
            dn = unit(np.array([0.0, -1.0, 0.0]) - n * (-n[1]))            # parts): the bore's width from its axis down
            lat = unit(np.cross(n, dn))                                    # to pl end_slot_to_y, along its way on
            depth = (hit[1] - pl["end_slot_to_y"]) / max(-dn[1], 0.2)
            L_ = max(thr, (abs(hit[0]) - 18.45) / max(abs(n[0]), 0.2) + 0.5)
            q4 = [hit + lat * tube / 2, hit - lat * tube / 2, hit - lat * tube / 2 + dn * depth, hit + lat * tube / 2 + dn * depth]
            end_slot = b.extrude(b.Face(b.Wire.make_polygon([b.Vector(*map(float, q)) for q in q4], close=True)),
                                 amount=float(L_), dir=tuple(map(float, -n))) + cyl(hit, -n, L_, tube)
        if bend is None:                                                   # on inwards until it leaves the grip
            on_bore = cyl(hit - n * thr, n, thr + 0.01, tube)
        else:
            on_bore = b.Pos(*map(float, hit)) * b.Sphere(tube / 2) + cyl(hit, inner, thr, tube)
        bore = tube_bore + on_bore
        if grow:                                   # the grip: a rectangular block round the held length inside
            bb = (held & form).bounding_box() if (held & form).solids() else held.bounding_box()
            blk = box(bb.min.X - grow, bb.max.X + grow, bb.min.Y - grow, bb.max.Y + grow, bb.min.Z - grow, bb.max.Z + grow)
            base_cuts.append(blk); cover_cuts.append(blk)
            continue
        # no wire channels (the user cuts them by hand): the tube's round bore and a plain rectangular pocket
        # round the JST plug's housing (pl housing: X0, X1, Y0, Y1, Z0, Z1), pl rear_room (1 mm) behind its rear for the wires
        hx0, hx1, hy0, hy1, hz0, hz1 = pl["housing"]
        g_ = cfg["housing_clear"]
        rr = pl.get("rear_room", 1.0)
        pocket = box(hx0 - g_ - (rr if side < 0 else 0), hx1 + g_ + (rr if side > 0 else 0), hy0 - g_, hy1 + g_, hz0 - g_, hz1 + g_)
        base_cuts.append(bore + pocket + (end_slot if end_slot is not None else b.Compound([])))   # (user 2026-10-11:
        cover_cuts.append(tube_bore + pocket + (end_slot if end_slot is not None else b.Compound([])))   # the way on cuts
                                                   # the base only; the cover gets the glued length through the corner)
        models += [cyl(hit, n, t_hit + cfg["tube_out_mm"], cfg["tube_od"])]          # the leg, its start straight
        info.append({"plug": pl["name"], "star": star.round(2).tolist(), "hit": hit.round(3).tolist(), "through_mm": round(float(thr), 2),
                     "off_corner_axis_deg": tilt, "inner_dir": np.round(inner, 3).tolist(),
                     "bend_deg": None if bend is None else round(math.degrees(math.acos(float(np.clip(bend @ (-n), -1, 1)))), 1),
                     "tube_axis": n.round(3).tolist(), "tube_held_mm": round(float(t_hit), 1)})
    return base_cuts, cover_cuts, models, info


def leds(form, base, cover, P, avoid):
    """the LED strips (pcb/LEDS: 37 x 5 x 1.6 designed, 37.2 x 5.6 as made (cfg board), LEDs at LEDS_AT along it, 3.5 cubes 0.76 tall, domes), two per
    lens on crater facets facing apart, each as far from the lens as it fits: recessed in the skin, the
    board's front cfg skin under the facet (the cubes flush in square holes, only the domes out), every
    dome under the lens's view line, in one part, cfg margin off the facet's edges, cfg star_clear off the
    legs' stars; it goes in straight along the facet's normal from the inside, so the board and cfg reach
    behind it are inside the form, clear of avoid (boxes X0, X1, Y0, Y1, Z0, Z1 that must stay: the
    simple structures, the legs). Returns ({part: [cuts]}, models, info)"""
    from matplotlib.path import Path
    cfg = P["leds"]
    L, W, T = cfg["board"]
    k = math.tan(math.radians(P["crater"]["slope_deg"]))
    stars = [np.array(pl["star"]) for pl in P["legs"]["plugs"]]
    av = np.array(avoid)
    def in_avoid_one(q, k_):
        return bool(((q[:, 0] > k_[0]) & (q[:, 0] < k_[1]) & (q[:, 1] > k_[2]) & (q[:, 1] < k_[3]) & (q[:, 2] > k_[4]) & (q[:, 2] < k_[5])).any())
    def in_avoid(q):
        return ((q[:, None, 0] > av[None, :, 0]) & (q[:, None, 0] < av[None, :, 1]) & (q[:, None, 1] > av[None, :, 2]) &
                (q[:, None, 1] < av[None, :, 3]) & (q[:, None, 2] > av[None, :, 4]) & (q[:, None, 2] < av[None, :, 5])).any()
    cuts, info, frames = {"base": [], "cover": []}, [], []
    for v in (1, -1):
        cand = []
        for f in form.faces():
            nv = f.normal_at(); n = np.array([nv.X, nv.Y, nv.Z]); c = f.center()
            if v * n[1] > cfg["min_facing"] and v * (c.Y - 7.85) > 8 and f.area > 300:
                cand.append((f, n))
        fits = []
        for f, n in cand:
            hz = np.array([n[0], 0.0, n[2]])
            h = unit(hz) if np.linalg.norm(hz) > 0.05 else np.array([1.0, 0.0, 0.0])
            V = np.array([[q.X, q.Y, q.Z] for q in f.outer_wire().vertices()]); o = V.mean(0)
            e1 = unit(np.cross(n, h)); e2 = unit(np.cross(n, e1))
            poly = Path(np.c_[(V - o) @ e1, (V - o) @ e2])
            lens = np.array([0.0, o[1], LENS_Z])
            tries = []
            longest = unit(max((V[(i + 1) % len(V)] - V[i] for i in range(len(V))), key=np.linalg.norm))
            for ang in [math.atan2(longest @ e2, longest @ e1)] + [math.radians(a) for a in range(0, 180, 15)]:
                ca, sa = math.cos(ang), math.sin(ang)
                hl, hw = L / 2 + cfg["margin"], W / 2 + cfg["margin"]
                corners = [np.array([ca * x - sa * y, sa * x + ca * y]) for x, y in ((-hl, -hw), (hl, -hw), (hl, hw), (-hl, hw))]
                for du in np.linspace(-25, 25, 51):
                    for dv in np.linspace(-25, 25, 51):
                        c2 = np.array([du, dv])
                        if all(poly.contains_point(c2 + q) for q in corners):
                            cen = o + du * e1 + dv * e2
                            tries.append((np.hypot(*(cen - lens)[[0, 2]]), cen, ang))
            tries.sort(key=lambda t: -t[0])
            why = {"star": 0, "view": 0, "avoid": 0, "skin": 0, "part": 0}
            for dist, cen, ang in tries:
                xd = math.cos(ang) * e1 + math.sin(ang) * e2; yd = np.cross(n, xd)
                if min(np.linalg.norm(cen - s_) for s_ in stars) < cfg["star_clear"] + L / 2:
                    # too near a star: check the strip's own line
                    seg = [cen + xd * t for t in np.linspace(-L / 2, L / 2, 15)]
                    if min(np.linalg.norm(p - s_) for p in seg for s_ in stars) < cfg["star_clear"]:
                        why["star"] += 1; continue
                tops = [cen + (x - L / 2) * xd + y * yd + n * cfg["dome"] for x in cfg["leds_at"] for y in (-1.75, 1.75)]
                if max(math.degrees(math.atan2(v * (q[1] - GLASS_RIM[v]), math.hypot(q[0], q[2] - LENS_Z) - 5.5)) for q in tops) > -P["crater"]["slope_deg"]:
                    why["view"] += 1; continue
                # the board's volume and the reach behind it
                pts = np.array([cen + x * xd + y * yd - d * n for x in np.linspace(-L / 2 - 0.2, L / 2 + 0.2, 13)
                                for y in (-W / 2 - 0.2, 0, W / 2 + 0.2) for d in (cfg["skin"] - 0.6 + 0.6, cfg["skin"] + T, cfg["skin"] + T + cfg["reach"])])
                if in_avoid(pts):
                    why["avoid"] += 1; continue
                part = "cover" if v > 0 else "base"
                piece = cover if part == "cover" else base
                edge = [cen + x * xd + y * yd - d * n for x in (-L / 2 - 0.8, L / 2 + 0.8) for y in (-W / 2 - 0.8, W / 2 + 0.8)
                        for d in (cfg["skin"] - 0.6, cfg["skin"] + T + 0.6)]
                if not all(piece.is_inside(b.Vector(*map(float, q))) for q in edge):
                    why["skin"] += 1; continue
                if not all(piece.is_inside(b.Vector(*map(float, q))) for q in pts[::3]):
                    why["part"] += 1; continue
                fits.append((dist, f, n, h, cen, xd, yd, part)); break
            else:
                info.append({"lens": v, "facet_facing": np.round(h, 2).tolist(), "area": round(f.area), "tries": len(tries), "failed": why})
        # two strips on opposite sides of the lens, as far from it as they fit
        side_of = lambda ft: unit(np.array([ft[4][0], 0.0, ft[4][2] - LENS_Z]))
        pairs = [(a_, c_) for i_, a_ in enumerate(fits) for c_ in fits[i_ + 1:] if side_of(a_) @ side_of(c_) < 0.0]
        chosen = max(pairs, key=lambda pc: pc[0][0] + pc[1][0]) if pairs else fits[:2]
        pins = [q for q in cfg.get("pinned", []) if q["part"] == ("cover" if v > 0 else "base")]
        if pins:                                   # the approved placement, fixed (user: the LEDs go in first, the
            chosen = [(math.hypot(q["o"][0], q["o"][2] - LENS_Z), None, np.array(q["n"]), np.array(q["n"]),   # legs
                       np.array(q["o"]), np.array(q["x"]), np.array(q["y"]), q["part"]) for q in pins]       # adapt)
        for dist, f, n, h, cen, xd, yd, part in chosen:
            loc = b.Location(b.Plane(origin=tuple(map(float, cen)), x_dir=tuple(map(float, xd)), z_dir=tuple(map(float, n))))
            seat = loc * b.Pos(0, 0, -cfg["skin"]) * b.Box(L + 0.4, W + 0.4, cfg["skin"] + T + cfg["reach"] - cfg["skin"],
                                                         align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX))
            holes = [loc * b.Pos(x - L / 2, 0, 0.1) * b.Box(cfg["cube"] + 0.1, cfg["cube"] + 0.1, cfg["skin"] + 0.2,
                                                            align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX)) for x in cfg["leds_at"]]
            cuts[part] += [seat] + holes
            frames.append({"part": part, "o": cen, "x": xd, "y": yd, "n": n})
            info.append({"lens": "top" if v > 0 else "bottom", "part": part, "from_lens_axis_mm": round(float(dist), 1),
                         "facet_facing": np.round(h, 2).tolist(), "centre": np.round(cen, 1).tolist(),
                         "z": [round(float((cen - xd * L / 2)[2]), 1), round(float((cen + xd * L / 2)[2]), 1)]})
        if len(chosen) < 2:
            info.append({"lens": "top" if v > 0 else "bottom", "missing": 2 - len(chosen), "facets_tried": len(cand), "fits": len(fits)})
    # the clamp, one per strip, in its middle: a flat plate pressing on the board's back as far along the
    # strip as there is room (the board cannot wobble), one long rib on it for stiffness, an ear in the
    # middle over a square boss rising from the skin beside the strip (on the side with room), one M2 screw
    # through the ear into the boss (cfg levels: clamp top, screw length). Strip frame: x along, y across, z out along the normal, 0 at the facet
    c = cfg["clamp"]
    he = c["ear_len"] / 2
    inner = b.offset(form, amount=-P["shell"]["skin_mm"], kind=b.Kind.INTERSECTION)   # the clamps stay off the skin
    sb = P["shell"]["seam_band"]                             # and off the band along the parting (kept solid)
    bands = {"base": b.Pos(0, -sb[0], 0) * cover, "cover": b.Pos(0, sb[1], 0) * base}
    back = cfg["skin"] + T                                   # the board's back below the facet
    ring = 1.1 + 1.0                                         # the ear round the screw's hole: >= 1 mm
    Wp = W - 2 * c["plate_inset"]                            # the plate a little narrower than the board (off the skin where facets fold)
    for fr in frames:
        o, xd, yd, n, part = fr["o"], fr["x"], fr["y"], fr["n"], fr["part"]
        piece = cover if part == "cover" else base
        P_ = lambda x, y, z: o + xd * x + yd * y + n * z
        band_ = bands[part]
        free = lambda pts, within=None: (not any(in_avoid_one(np.array(pts), k_) for k_ in avoid)) and \
            all((within or piece).is_inside(b.Vector(*map(float, q))) for q in (pts if within else pts[::7])) and \
            (within is None or not any(band_.is_inside(b.Vector(*map(float, q))) for q in pts))
        # the plate over the whole strip, its tongue out past one end into a slot block standing on the skin
        # there (the end where that block fits); the clamp slides along the strip into the slot, then the one
        # screw in the ear. Strip frame: x along, y across, z out (depth = -z)
        sb, sl_ = c["slot_block"], c["slot"]
        def block_pts(sA):
            x0_, x1_ = L / 2 + 0.3, L / 2 + 0.3 + sb
            return [P_(sA * x_, y, -z) for x_ in np.arange(x0_, x1_ + 0.01, 0.5)
                    for y in np.arange(-(Wp / 2 + 1.5), Wp / 2 + 1.51, 0.5) for z in np.arange(2.2, back + c["plate_t"] + 1.5 + 0.01, 0.5)]
        ends_ok = [sA for sA in (1, -1) if free(block_pts(sA), piece)]   # (it may merge into the skin)
        sA = ends_ok[0] if ends_ok else 0
        x34 = -sA * L / 4                                    # the screw 3/4 of the strip from the slot
        tried = []
        for xc, (top, slen) in [(xc_, lv) for xc_ in [x34 + d_ for d_ in (0, 1, -1, 2, -2, 3, -3, 4, -4)]
                                for lv in c["levels"]]:   # 3/4 from the slot (full height, else low); a little off it if nothing fits
            bt = top - c["ear_t"] - c["gap"]              # the boss's top: the ear on it, the screw's tip at pilot_end
            for side in (1, -1):                          # either side of the strip
                for by in c["boss_y"]:
                    yb = side * by
                    y0, y1 = sorted((-side * (W / 2), yb + side * max(c["boss_r"], ring)))
                    grid = [P_(xc + dx, y, -z) for dx in np.arange(-he, he + 0.01, 0.5) for y in np.arange(y0, y1 + 0.01, 0.5)
                            for z in np.arange(back, top + 0.01, 0.5)] + \
                           [P_(xc + 1.75 * math.cos(t_), yb + 1.75 * math.sin(t_), -z) for t_ in np.linspace(0, 2 * math.pi, 12, endpoint=False)
                            for z in (top + 0.5, top + 1.1)] + [P_(xc, yb, -top - 1.1)] + \
                           [P_(xc + dx, yb + dy, -z) for dx in np.arange(-c["boss_r"], c["boss_r"] + 0.01, 0.5)
                            for dy in np.arange(-c["boss_r"], c["boss_r"] + 0.01, 0.5) for z in np.arange(2.0, bt + 0.01, 0.5)]
                    ring_ = [P_(xc + 1.8 * math.cos(t_), yb + 1.8 * math.sin(t_), -z) for t_ in np.linspace(0, 2 * math.pi, 16, endpoint=False)
                             for z in np.arange(top - slen, bt + 0.01, 0.5)]     # >= 1 mm round the pilot, to its tip
                    ok = free(grid) and all(piece.is_inside(b.Vector(*map(float, q))) for q in ring_)
                    why_ = [] if ok else [list(map(float, k_)) for k_ in avoid if in_avoid_one(np.array(grid), k_)] or ["outside the part"]
                    tried.append({"x": xc, "top": top, "side": side, "boss_y": by, "blocked": why_[:2]})
                    if ok:
                        break
                else:
                    continue
                break
            else:
                continue
            break
        else:
            info.append({"clamp": "no room", "part": part, "tried": tried}); continue
        xa, xb = -(L / 2 - 0.5), L / 2 - 0.5
        if sA > 0:
            xb = L / 2 + 0.3 + sl_ - 0.2
        elif sA < 0:
            xa = -(L / 2 + 0.3 + sl_ - 0.2)
        fr["side"], fr["xc"], fr["clamp_span"], fr["top"], fr["screw_len"] = side, xc, [float(xa), float(xb)], top, slen
        info.append({"clamp": part, "x": float(xc), "top": top, "screw": f"M2x{slen:g}", "side": side, "slot_end": sA,
                     "middle_tried": [t_ for t_ in tried if t_["x"] == c["x"] and t_["blocked"]], "boss_y": by, "plate": [round(float(xa), 1), round(float(xb), 1)]})
        loc = b.Location(b.Plane(origin=tuple(map(float, o)), x_dir=tuple(map(float, xd)), z_dir=tuple(map(float, n))))
        boss = loc * b.Pos(xc, yb, -2.0 + 0.5) * b.Box(2 * c["boss_r"], 2 * c["boss_r"], bt - 2.0 + 0.5, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX))
        pilot = loc * b.Pos(xc, yb, -(top - slen)) * b.Cylinder(0.8, bt - (top - slen) + 0.5, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX))
        fr["boss"] = boss
        cuts[part].append(pilot)
        fr["screw"] = P_(xc, yb, -top - 1.1)               # its head's top (ISO 7380 M2: 1.1 high)
        # the clamp, in place: plate + rib along the strip, the ear from the board's far edge to past the
        # boss, the boss's room under the ear, the screw's hole, the wire pads' channel across the middle
        y0, y1 = sorted((-side * (W / 2), yb + side * max(c["boss_r"], ring)))
        mid = (xa + xb) / 2
        plate = b.Pos(mid, 0, -back - c["plate_t"] / 2) * b.Box(xb - xa, Wp, c["plate_t"])
        ra, rb = max(xa, -L / 2), min(xb, L / 2)                     # the rib stays out of the slot; full height
        rw, low = c["rib_w"], back + c["plate_t"] + 0.4              # round the ear, chamfered down toward the ends
        e0, e1 = max(ra, xc - he - 1.0), min(rb, xc + he + 1.0)
        prof = [(ra, back), (rb, back), (rb, low), (e1, top), (e0, top), (ra, low)]
        rib = b.extrude(b.Face(b.Wire.make_polygon([b.Vector(x_, -rw / 2, -d_) for x_, d_ in prof], close=True)),
                        amount=rw, dir=(0, 1, 0))
        if sA:                                                       # the slot block and its slot
            bx0 = L / 2 + 0.3
            blk_ = loc * b.Pos(sA * (bx0 + sb / 2), 0, -(1.0 + back + c["plate_t"] + 1.5) / 2) * \
                b.Box(sb, Wp + 3.0, back + c["plate_t"] + 1.5 - 1.0)
            slot_ = loc * b.Pos(sA * (bx0 - 0.1 + sl_ / 2 + 0.05), 0, -(back + c["plate_t"] / 2)) * \
                b.Box(sl_ + 0.1, Wp + 0.4, c["plate_t"] + 0.4)
            fr["slot_block"] = blk_
            cuts[part].append(slot_)
        ear = b.Pos(xc, (y0 + y1) / 2, -(back + top) / 2) * b.Box(2 * he, y1 - y0, top - back)
        room = b.Pos(xc, yb, -(bt - c["gap"] + 1.0) / 2) * b.Box(2 * he + 1, 2 * c["boss_r"] + 0.6, bt - c["gap"] + 1.0)
        hole = b.Pos(xc, yb, -top / 2) * b.Cylinder(1.1, top + 1)
        wires = b.Pos(0, 0, -back - c["wire_channel"] / 2 + 0.01) * b.Box(c["wire_gap"], W + 2 * max(c["boss_y"]) + 6, c["wire_channel"] + 0.02)
        clamp_ = loc * ((plate + rib + ear) - room - hole - wires)
        fr["clamp"] = clamp_.clean()
        g, wi, sd = 0.3, cfg["clamp_way"], sl_ + 0.5           # its envelope, 0.3 grown; its way in: down along -n
        def env(ext):                                        # (the boxes deeper), slid back out of the slot (longer)
            xa_e, xb_e = xa - (sd if sA > 0 and ext else 0), xb + (sd if sA < 0 and ext else 0)
            m_ = (xa_e + xb_e) / 2
            return loc * (
                b.Pos(m_, 0, -back - c["plate_t"] / 2 - ext / 2) * b.Box(xb_e - xa_e + 2 * g, Wp + 2 * g, c["plate_t"] + 2 * g + ext) +
                b.Pos(m_, 0, -(back + top) / 2 - ext / 2) * b.Box(xb_e - xa_e + 2 * g, c["rib_w"] + 2 * g, top - back + 2 * g + ext) +
                b.Pos(xc, (y0 + y1) / 2, -(back + top) / 2 - ext / 2) * b.Box(2 * he + 2 * g + (sd if ext else 0), y1 - y0 + 2 * g, top - back + 2 * g + ext))
        fr["clamp_env"], fr["clamp_way"] = env(0.0), env(wi)
    return cuts, info, frames


def hollow(form, piece, other, part, cfg, keep_extra=(), clear=(), extra_boxes=(), force=()):
    """the part's hollow: everything inside the cfg skin_mm skin except what has a job - the band along the
    parting (cfg seam_band: lip, rebate, clips; open inside band_open_<part>), the blocks cfg keep_<part> and
    extra_boxes (rectangular, each joined to its nearest wall along X or Z: none stands alone) and keep_extra
    (LED bosses and slot blocks, the legs' grips). Then the LED ways in (clear) are cut, a block they
    cross cut square (its box), the bosses and screw blocks kept."""
    inset = b.offset(form, amount=-cfg["skin_mm"], kind=b.Kind.INTERSECTION)
    sgn = -1 if part == "base" else 1                       # into the part from the seam (the band below/above it)
    depth = cfg["seam_band"][0] if part == "base" else cfg["seam_band"][1]
    r2 = depth / math.sqrt(2)                              # the other part moved into this one, straight and
    band = b.Pos(0, sgn * depth, 0) * other                # slanted: depth across the parting wherever it slopes
    for v in [(depth, 0, 0), (-depth, 0, 0), (0, 0, depth), (0, 0, -depth),       # (a straight Y shift left 2 cos 67 =
              (r2, sgn * r2, 0), (-r2, sgn * r2, 0), (0, sgn * r2, r2), (0, sgn * r2, -r2)]:   # 0.8 mm on the side walls)
        band = band + b.Pos(*v) * other
    band = band & piece
    if cfg.get("band_open_" + part):                       # inside the lip ring the band holds nothing
        x0, x1, z0, z1 = cfg["band_open_" + part]
        band = band - b.Pos((x0 + x1) / 2, 0, (z0 + z1) / 2) * b.Box(x1 - x0, BIG, z1 - z0)
    bb = piece.bounding_box()
    inside = lambda q: inset.is_inside(b.Vector(*map(float, q)))
    # the blocks, each joined to its nearest wall (the inset) along X or Z: no block stands alone
    boxes = [list(k) for k in cfg["keep_" + part] if k[0] != "cyl"] + [list(k) for k in extra_boxes]
    for i, k in enumerate(boxes):
        ym = min(max((k[2] + k[3]) / 2, bb.min.Y + 0.5), bb.max.Y - 0.5)
        best = None
        for ax, sg_ in ((0, -1), (0, 1), (2, -1), (2, 1)):
            face = k[ax * 2 + (1 if sg_ > 0 else 0)]
            o = 2 if ax == 0 else 0                           # the other horizontal axis: probe its middle and ends
            for d in np.arange(0.0, 40.0, 0.5):
                hit = False
                for u in ((k[2 * o] + k[2 * o + 1]) / 2, k[2 * o] + 0.3, k[2 * o + 1] - 0.3):
                    q = [0.0, ym, 0.0]; q[ax] = face + sg_ * (d + 0.25); q[o] = u
                    if not inside(q) or any(all(bx_[2 * m] <= q[m] <= bx_[2 * m + 1] for m in range(3))
                                            for j, bx_ in enumerate(boxes) if j != i and len(bx_) > 6):
                        hit = True; break                        # the skin or a kept (protected) block
                if hit:
                    break
            if best is None or d < best[0]:
                best = (d, ax, sg_)
        d, ax, sg_ = best
        if d > 0.01:
            k[ax * 2 + (1 if sg_ > 0 else 0)] += sg_ * (d + 0.5)
            cfg.setdefault("_joined", []).append([part, i, "XYZ"[ax] + ("+" if sg_ > 0 else "-"), round(float(d), 1)])
    # everything inside the skin goes but the band, the blocks and keep_extra (no inner plastic without a job)
    void = (inset & piece) - band
    for k in boxes:
        void = void - b.Pos((k[0] + k[1]) / 2, (k[2] + k[3]) / 2, (k[4] + k[5]) / 2) * b.Box(k[1] - k[0], k[3] - k[2], k[5] - k[4])
    for s_ in keep_extra:
        void = void - s_
    for s_ in clear:                                       # the LED ways in, never through the band ...
        c_ = (s_ & inset & piece) - band
        for k in boxes:                                    # ... a block they cross cut square (its box: no needles)
            if len(k) > 6 and k[6] in ("screw", "seat"):
                continue
            bx = b.Pos((k[0] + k[1]) / 2, (k[2] + k[3]) / 2, (k[4] + k[5]) / 2) * b.Box(k[1] - k[0], k[3] - k[2], k[5] - k[4])
            hit = bx & s_
            if hit is None or not isinstance(hit, b.Shape) or not hit.solids():
                continue
            hb = hit.bounding_box()
            c_ = c_ + ((b.Pos((hb.min.X + hb.max.X) / 2, (hb.min.Y + hb.max.Y) / 2, (hb.min.Z + hb.max.Z) / 2) *
                        b.Box(hb.max.X - hb.min.X, hb.max.Y - hb.min.Y, hb.max.Z - hb.min.Z)) & bx & inset & piece) - band
        void = void + c_
    for s_ in force:                                       # marked open: everything inside the skin there, the band too,
        f_ = s_ & inset & piece                            # but the blocks
        for k in boxes:
            f_ = f_ - b.Pos((k[0] + k[1]) / 2, (k[2] + k[3]) / 2, (k[4] + k[5]) / 2) * b.Box(k[1] - k[0], k[3] - k[2], k[5] - k[4])
        void = void + f_
    for s_ in keep_extra:                                  # the bosses and the legs' grips stay,
        void = void - s_
    for k in boxes:                                        # and the blocks round screw holes
        if len(k) > 6 and k[6] in ("screw", "seat"):
            void = void - b.Pos((k[0] + k[1]) / 2, (k[2] + k[3]) / 2, (k[4] + k[5]) / 2) * b.Box(k[1] - k[0], k[3] - k[2], k[5] - k[4])
    pieces = [s_ for s_ in void.solids() if s_.volume >= cfg["min_void_mm3"]]
    return b.Compound(pieces), pieces


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("params", nargs="?", default=os.path.join(HERE, "form_params.json"))
    ap.add_argument("--seed", type=int)
    ap.add_argument("--out", default=HERE)
    a = ap.parse_args()
    P = json.load(open(a.params))
    report = {}
    if a.seed is not None:                                       # random starting values, to adjust by hand
        rng = np.random.default_rng(a.seed)
        for f in P["faces"].values():
            f["roll_deg"] = round(float(rng.choice([-1, 1]) * rng.uniform(3, 9)), 1)
            f["pitch_deg"] = round(float(rng.choice([-1, 1]) * rng.uniform(2, 6)), 1)
        for e in P["ends"].values():
            e["yaw_deg"] = round(float(rng.uniform(-10, 10)), 1)
            e["pitch_deg"] = round(float(rng.uniform(-7, 7)), 1)
        P["crater"]["top_rotation_deg"] = round(float(rng.uniform(0, 90)), 1)
        P["crater"]["bottom_rotation_deg"] = round(float(rng.uniform(0, 90)), 1)
        report["params"] = P
    form = form_solid(P, report)
    b.export_step(form, os.path.join(a.out, "form_outer.step"))      # the bare outer form, for concept renders
    up = P.get("lens_raise_mm", 0.0)
    lenses = [b.Pos(0, 29.9 + up, LENS_Z) * b.Cylinder(7.5, 8.0, rotation=(-90, 0, 0), align=(b.Align.CENTER, b.Align.CENTER, b.Align.MIN)),
              b.Pos(0, -14.2 - up, LENS_Z) * b.Cylinder(7.5, 8.0, rotation=(90, 0, 0), align=(b.Align.CENTER, b.Align.CENTER, b.Align.MIN))]
    b.export_step(b.Compound([form] + lenses), os.path.join(a.out, "form_concept.step"))
    form0 = form                             # the form's own vertices: stars, parting loop
    leg_blocks = [k[:6] for pt in ("base", "cover") for k in P.get("shell", {}).get("keep_" + pt, [])] + \
                 [k[:6] for k in P.get("shell", {}).get("board_supports", [])]
    grooves, seats, leg_models, report["legs"] = legs(form0, P["legs"], blocks=leg_blocks)
    base, cover, sheet = parting(form0, P["parting"])
    for x0, x1, z0, z1 in P["parting"].get("flat_rects", []):   # the seam flat at YS there (round the end seam
        prism = b.Pos((x0 + x1) / 2, (YS - 60) / 2, (z0 + z1) / 2) * b.Box(x1 - x0, YS + 60, z1 - z0)   # screws):
        base = base + (prism & form0)                            # the base up to it, the cover cut square above
        cover = cover - prism
    for poly, z0, z1 in P["parting"].get("base_prisms", []):    # regions moved from the cover to the base (a crag in
        face = b.Face(b.Wire.make_polygon([b.Vector(x_, y_, z0) for x_, y_ in poly], close=True))   # the seam filled)
        prism = b.extrude(face, amount=z1 - z0, dir=(0, 0, 1))
        base = base + (prism & form0)
        cover = cover - prism
    base, cover, form = base.solids()[0].clean(), cover.solids()[0].clean(), form.solids()[0].clean()   # coplanar faces merged
    b.export_step(form, os.path.join(a.out, "form_outer.step"))
    b.export_step(base, os.path.join(a.out, "base_outer.step"))          # each SolidWorks part starts from its piece
    b.export_step(cover, os.path.join(a.out, "cover_outer.step"))
    report["parting"] = {"base_mm3": round(base.volume), "cover_mm3": round(cover.volume)}

    # cuts the user marked with sketches in the fit (profile in part coordinates, swept along dir from d0 to d1;
    # skin: not nearer the outside than the 2 mm skin)
    inner_ = b.offset(form0, amount=-P["shell"]["skin_mm"], kind=b.Kind.INTERSECTION)
    def user_prism(uc):                                  # a marked sketch: its profile from d0 to d1 along dir
        d = unit(uc["dir"])
        q0 = np.array(uc["pts"][0], float)                   # onto the sketch's plane (rounded points)
        pts_ = [np.array(q, float) - d * ((np.array(q, float) - q0) @ d) for q in uc["pts"]]
        face = b.Face(b.Wire.make_polygon([b.Vector(*(q + d * uc["d0"])) for q in pts_], close=True))
        pr = b.extrude(face, amount=uc["d1"] - uc["d0"], dir=tuple(map(float, d)))
        sk_ = uc.get("skin")                                  # True: the 2 mm skin; a number: that much
        if sk_:
            return pr & (inner_ if sk_ is True else b.offset(form0, amount=-float(sk_), kind=b.Kind.INTERSECTION))
        return pr
    user_cuts = {"base": [], "cover": []}                # (removing plastic: not something the LED parts avoid)
    for part_, lst in user_cuts.items():
        for uc in P.get("user_cuts", {}).get(part_, []):
            lst.append(user_prism(uc))
    user_fills = {"base": [user_prism(uf) for uf in P.get("user_fills", {}).get("base", [])],     # marked solid: kept
                  "cover": [user_prism(uf) for uf in P.get("user_fills", {}).get("cover", [])]}   # by the hollow
    b.export_step(b.Compound(grooves + user_cuts["base"]), os.path.join(a.out, "legs_base.step"))     # the base: bores,
    b.export_step(b.Compound(seats + user_cuts["cover"]), os.path.join(a.out, "legs_cover.step"))     # JST pockets, marked cuts
    b.export_step(b.Compound(leg_models), os.path.join(a.out, "probe_legs.step"))       # tubes and wires, for the assembly
    if P.get("leds"):                        # the LED strips: in the skin under the crater facets
        avoid = [list(k) for k in P["leds"]["avoid"]]   # the real features near them (camera pockets, screw pilots)
        avoid += [list(k[:6]) for pt in ("base", "cover") for k in P["shell"]["keep_" + pt]
                  if isinstance(k, list) and len(k) > 6 and k[6] == "screw"]   # the screw blocks stay solid over the ways in
        led_cuts, report["leds"], led_frames = leds(form0, base, cover, P, avoid)
        for part in ("base", "cover"):
            b.export_step(b.Compound(led_cuts[part]), os.path.join(a.out, f"leds_{part}.step"))
        # for the assembly (pcb/LEDS.SLDPRT placed per strip frame, the clamps in place)
        cfg = P["leds"]
        for i, fr in enumerate([f_ for f_ in led_frames if "clamp" in f_], 1):       # each in place (assembly at identity)
            w_ = fr["clamp"].solids()[0].wrapped                 # the placement baked in: a placed shape is an assembly in SolidWorks
            w_ = BRepBuilderAPI_Transform(w_.Located(TopLoc_Location()), w_.Location().Transformation(), True).Shape()
            b.export_step(b.Solid(w_), os.path.join(a.out, f"led_clamp_{i}.step"))
        poses = []
        for fr in [f_ for f_ in led_frames if "side" in f_]:     # the strips that got a clamp
            sd = fr["side"]
            xd, yd, n = fr["x"], fr["y"] * sd, fr["n"]
            xd = xd if sd > 0 else -xd                        # side -1: the frame turned half round the normal
            poses.append({"part": fr["part"], "strip": [list(map(float, fr["x"])) + list(map(float, fr["y"])) + list(map(float, n)), list(map(float, fr["o"]))],
                          "clamp": [list(map(float, xd)) + list(map(float, yd)) + list(map(float, n)), list(map(float, fr["o"] + fr["x"] * fr.get("xc", 0.0)))],
                          "screw": [list(map(float, n)) + list(map(float, xd)) + list(map(float, np.cross(n, xd))), list(map(float, fr.get("screw", fr["o"])))],
                          "normal": list(map(float, n)), "top": float(fr["top"]), "screw_len": float(fr["screw_len"])})
        json.dump(poses, open(os.path.join(a.out, "led_poses.json"), "w"), indent=1)
        # the board's supports (posts from the bottom skin up to it, edge tabs) in a base strip's way in go:
        # the board rests on the others
        ways = [b.Location(b.Plane(origin=tuple(map(float, fr["o"])), x_dir=tuple(map(float, fr["x"])), z_dir=tuple(map(float, fr["n"])))) *
                b.Box(cfg["board"][0] + 2, cfg["board"][1] + 2, 16.0, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX))
                for fr in led_frames if fr["part"] == "base"]
        supports, support_boxes, report["board_supports_dropped"] = [], [], []
        for k_ in P["shell"].get("board_supports", []):
            bx = b.Pos((k_[0] + k_[1]) / 2, (k_[2] + k_[3]) / 2, (k_[4] + k_[5]) / 2) * b.Box(k_[1] - k_[0], k_[3] - k_[2], k_[5] - k_[4])
            if any((lambda x_: isinstance(x_, b.Shape) and x_.solids() and x_.volume > 0.01)(bx & w_) for w_ in ways):
                report["board_supports_dropped"].append(k_)
            else:
                support_boxes.append(k_)
        clamp_room = {"base": [], "cover": []}
        for fr in [f_ for f_ in led_frames if "clamp" in f_]:
            clamp_room[fr["part"]].append(fr["clamp_env"])
            # the way in for the strip and the clamp: their outline swept along -n (strip_way, clamp_way: the straight push at the end), kept free
            loc = b.Location(b.Plane(origin=tuple(map(float, fr["o"])), x_dir=tuple(map(float, fr["x"])), z_dir=tuple(map(float, fr["n"]))))
            L_, W_, T_ = cfg["board"]
            clamp_room[fr["part"]].append(loc * b.Pos(0, 0, -cfg["skin"]) *
                                          b.Box(L_ + 0.6, W_ + 0.6, T_ + cfg["strip_way"], align=(b.Align.CENTER, b.Align.CENTER, b.Align.MAX)))
            clamp_room[fr["part"]].append(fr["clamp_way"])                # the clamp's way in
            # the screw's: its head (Ø3.5) along the axis from inside to the clamp
            sl = b.Location(b.Plane(origin=tuple(map(float, fr["screw"])), z_dir=tuple(map(float, -fr["n"]))))
            clamp_room[fr["part"]].append(sl * b.Cylinder(2.2, cfg["clamp_way"] + 2, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MIN)))
        led_bosses = {pt_: (supports if pt_ == "base" else []) + [fr[k_] for fr in led_frames if fr["part"] == pt_
                                                                   for k_ in ("boss", "slot_block") if k_ in fr]
                      for pt_ in ("base", "cover")}
    if P.get("shell"):                       # the hollows
        walls_b, walls_c, _, _ = legs(form0, P["legs"], grow=P["legs"]["grip_mm"], blocks=leg_blocks)
        if P.get("leds"):                    # a leg's grip (its bore's box) gives way to a clamp: the clamp stays whole
            envs = {pt_: [fr["clamp_env"] for fr in led_frames if fr["part"] == pt_ and "clamp" in fr] for pt_ in ("base", "cover")}
            walls_b, walls_c = ([w_ - e_ if (w_ & e_).solids() else w_ for w_ in ws for e_ in [sum(envs[pt_][1:], envs[pt_][0])]]
                                if envs[pt_] else ws for ws, pt_ in ((walls_b, "base"), (walls_c, "cover")))
        if P.get("leds"):                    # so do the marked fills: the strips, clamps and screws get in
            for pt_ in ("base", "cover"):
                room_ = sum(clamp_room[pt_][1:], clamp_room[pt_][0]) if clamp_room[pt_] else None
                user_fills[pt_] = [f_ - room_ if room_ is not None and (f_ & room_).solids() else f_ for f_ in user_fills[pt_]]
        side_force = {"base": [], "cover": []}
        for sf in P["shell"].get("side_fills", []):      # one 45 deg face along a side wall: solid above, open below
            pt_, sd, x0, (z0, z1) = sf["part"], sf["side"], sf["x0"], sf["z"]
            y0 = sf["y0_min"]                              # its height at the pocket wall: at least y0_min, and above
            zone = b.Pos(sd * (x0 + 30), 0, (z0 + z1) / 2) * b.Box(60, 200, z1 - z0)   # every LED way in (0.3 clear)
            for r_ in clamp_room[pt_] if P.get("leds") else []:
                h_ = r_ & zone
                for v_ in (h_.vertices() if h_ is not None and h_.solids() else []):
                    y0 = max(y0, v_.Y + (sd * v_.X - x0) + 0.3)
            prof = lambda pts: b.extrude(b.Face(b.Wire.make_polygon([b.Vector(sd * u, v, z0) for u, v in pts], close=True)), amount=z1 - z0, dir=(0, 0, 1))
            fz1 = sf.get("fill_to_z", z1)                   # the fill ends there; beyond it everything inside the skin is
            fill_ = prof([(x0, y0), (x0, 40), (60, 40), (60, y0 - (60 - x0))])   # open (the other part dips below the seam)
            user_fills[pt_].append(fill_ & (b.Pos(0, 0, (z0 + fz1) / 2) * b.Box(400, 400, fz1 - z0)))
            side_force[pt_].append(prof([(x0, y0), (60, y0 - (60 - x0)), (60, -60), (x0, -60)]))
            if fz1 < z1:
                side_force[pt_].append(b.Pos(sd * (x0 + 30), 0, (fz1 + z1) / 2) * b.Box(60, 200, z1 - fz1))
            report.setdefault("side_fills", []).append({"part": pt_, "side": sd, "y_at_wall": round(y0, 2)})
        report["shell"] = {}
        voids = {}
        for part, piece, other, walls in (("base", base, cover, walls_b), ("cover", cover, base, walls_c)):
            h, pieces = hollow(form0, piece, other, part, P["shell"], list(walls) + user_fills[part] + (led_bosses[part] if P.get("leds") else []),
                               clamp_room[part] if P.get("leds") else [], support_boxes if part == "base" and P.get("leds") else [],
                               side_force[part])
            b.export_step(h, os.path.join(a.out, f"shell_{part}.step"))
            voids[part] = h
            report["shell"][part] = {"pieces": len(pieces), "hollow_mm3": round(sum(p_.volume for p_ in pieces)),
                                     "joined": [j[1:] for j in P["shell"].get("_joined", []) if j[0] == part]}
        # each bore runs on inwards until its whole end opens into a hollow (or leaves both parts): no blind ends
        # in a kept block (a seam wall, a board support) beyond its grip
        tube = P["legs"]["tube_od"] + P["legs"]["fit"]
        exts = {}
        for k_, li in enumerate(report["legs"]):
            n_, hit_ = -np.array(li["inner_dir"]), np.array(li["hit"])
            u_ = unit(np.cross(n_, [0, 1, 0]) if abs(n_[1]) < 0.9 else np.cross(n_, [1, 0, 0])); v_ = np.cross(n_, u_)
            ring_ = [tube / 2 * 0.95 * (math.cos(t_) * u_ + math.sin(t_) * v_) for t_ in np.linspace(0, 2 * math.pi, 12, endpoint=False)] + [np.zeros(3)]
            def open_(t):
                for d_ in ring_:
                    q = b.Vector(*map(float, hit_ - n_ * t + d_))
                    if base.is_inside(q) and not voids["base"].is_inside(q):   # (the way on cuts the base only)
                        return False
                return True
            t0 = li["through_mm"]; t = t0
            while li["bend_deg"] is None and t < t0 + 25 and not open_(t):   # (a bent bore ends in its plug's pocket)
                t += 0.5
            li["through_mm"] = round(t + (0.3 if t > t0 else 0.0), 2)
            if t > t0:
                exts[k_] = (b.Location(b.Plane(origin=tuple(map(float, hit_ - n_ * (t + 0.3))), z_dir=tuple(map(float, n_)))) *
                            b.Cylinder(tube / 2, t + 0.3 - t0 + 0.5, align=(b.Align.CENTER, b.Align.CENTER, b.Align.MIN)))
        if exts:                             # fused into the plug's own bore (separate coaxial bodies fail SolidWorks' combine)
            grooves = [g_ + exts[k_] if k_ in exts else g_ for k_, g_ in enumerate(grooves)]   # (the base only)
            b.export_step(b.Compound(grooves + user_cuts["base"]), os.path.join(a.out, "legs_base.step"))
            b.export_step(b.Compound(seats + user_cuts["cover"]), os.path.join(a.out, "legs_cover.step"))
    json.dump(report, open(os.path.join(a.out, "form_report.json"), "w"), indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
