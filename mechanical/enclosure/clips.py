"""Seam lip, seam screws, shared by build_base.py and build_cover.py (prepended to either).

The cover's lip (0.65 x 1.0) runs round the whole seam inside the walls' thickness, its inner face flush
with the pocket, and sits in a rebate in the base's wall tops: the walls cannot wobble. Designed with 0.2
clearance (the printer adds about 0.2), printed it is tight. It is broken only where something crosses the
seam. No snap clips or guide pins (user, 2026-10-10): the lip locates the cover; four M2x16 thread-forming screws from the base's
bottom hold the parts: heads in counterbores, clearance holes up through the base, pilots in the cover.
Lip and rebate are drawn on the Top Plane (sketch x, y = model X, -Z).
"""
import json, math, os
LIP_CLEAR = 0.2
SEAM_Z = (-33.6, 32.8)                          # the lip's inner outline at the ends: the cover's cell pocket end (camera),
                                                # the base's board pocket end (antenna); the sides at the pockets' X +-18.45
REBATE_T, REBATE_H = 0.85, 1.2                  # base rebate: width into the wall, depth below the seam (lip 0.65 x 1.0)
LIP_T, LIP_H = REBATE_T - LIP_CLEAR, REBATE_H - LIP_CLEAR
SEAM_SCREWS = [(x, z) for z in (-37.0, 35.8) for x in (-11.8, 11.8)]   # X, Z: 1 mm of material round every hole (board pocket end 32.8, J4 flex X -9.1..8.4)
SCREW_SEAT, SCREW_L, HEAD_H = -6.0, 16.0, 1.1   # counterbore floor (the base is shallow at the ends), M2x16 button head
SCREW_CB, SCREW_HOLE, SCREW_PILOT = 1.9, 1.1, 0.8   # radii: hole 0.2 round the M2 (Ø2.2), pilot Ø1.6


def top_span(rects=(), circles=(), ya=0.0, yb=0.0, boss=False):
    """rectangles (x0, z0, x1, z1) / circles (x, z, r) on the Top Plane, bossed or cut over Y ya..yb"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for x0, z0, x1, z1 in rects:
        sm.CreateCornerRectangle(x0 * M, -z0 * M, 0, x1 * M, -z1 * M, 0)
    for x, z, r in circles:
        sm.CreateCircleByRadius(x * M, -z * M, 0, r * M)
    sm.InsertSketch(True)
    if boss:
        return fm.FeatureExtrusion3(True, False, False, 0, 0, (yb - ya) * M, 0, False, False, False, False, 0, 0,
                                    False, False, False, False, True, True, True, 3, abs(ya) * M, ya < 0)
    return fm.FeatureCut4(True, False, True, 0, 0, (yb - ya) * M, 0, False, False, False, False, 0, 0,
                          False, False, False, False, False, True, True, False, False, False,
                          3, abs(ya) * M, ya < 0, False)




def tp_feature(name, rects, y0, y1, boss, expect, circles=(), optional=False):
    """rectangles (x0, z0, x1, z1) and circles (x, z, r) on the Top Plane, from Y y0 up to y1: a boss
    or a cut (uses the build script's model, sm, fm, ext, fbox); optional: a cut that finds no material
    (SolidWorks refuses it) is reported and skipped"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for x0, z0, x1, z1 in rects:
        sm.CreateCornerRectangle(x0 * M, -z0 * M, 0, x1 * M, -z1 * M, 0)
    for x, z, r in circles:
        sm.CreateCircleByRadius(x * M, -z * M, 0, r * M)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(y0) * M, y0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(y0) * M, y0 < 0, False)
    if f is None:
        if optional:
            print(f"{name:26s} nothing to cut here: skipped")
            return None
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def rp_feature(name, zy, x0, x1, boss, expect):
    """polygon (Z, Y) on the Right Plane, extruded through X x0..x1: a boss or a cut"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Right Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    pts = [(-z, y) for z, y in zy]
    for (a, b), (c, d) in zip(pts, pts[1:] + pts[:1]):
        sm.CreateLine(a * M, b * M, 0, c * M, d * M, 0)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(x0) * M, x0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(x0) * M, x0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def fp_feature(name, xy, z0, z1, boss, expect):
    """polygon (X, Y) on the Front Plane, extruded through Z z0..z1: a boss or a cut"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Front Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for (a, b), (c, d) in zip(xy, xy[1:] + xy[:1]):
        sm.CreateLine(a * M, b * M, 0, c * M, d * M, 0)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (z1 - z0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(z0) * M, z0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (z1 - z0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(z0) * M, z0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def ring(pocket_x, z_cam, z_ant, t, inset=0.0):
    """outer and inner rectangle of the band t wide outside (X +-pocket_x, Z z_cam..z_ant)"""
    return [(-pocket_x - t, z_cam - t, pocket_x + t, z_ant + t),
            (-pocket_x + inset, z_cam + inset, pocket_x - inset, z_ant - inset)]


def seam_cover_features(pocket_x, z_cam, z_ant, side_gaps, end_gaps):
    """side_gaps {side: [(z0, z1)]}, end_gaps {end: [(x0, x1)]}: where the lip must stay open"""
    tp_feature("Seam lip", ring(pocket_x, z_cam, z_ant, LIP_T), PARTING - LIP_H, PARTING + 0.01, True,
               f"{LIP_T} x {LIP_H} round the seam")
    gaps = [((pocket_x - 0.1, z0, pocket_x + LIP_T + 0.1, z1) if s > 0 else (-pocket_x - LIP_T - 0.1, z0, -pocket_x + 0.1, z1))
            for s, zs in side_gaps.items() for z0, z1 in zs]
    gaps += [((x0, z_ant - 0.1, x1, z_ant + LIP_T + 0.1) if e > 0 else (x0, z_cam - LIP_T - 0.1, x1, z_cam + 0.1))
             for e, xs in end_gaps.items() for x0, x1 in xs]
    tp_feature("Seam lip gaps", gaps, PARTING - LIP_H - 0.1, PARTING, False, f"{len(gaps)}: what crosses the seam")


def seam_base_features(pocket_x, z_cam, z_ant):
    """the rebate for the cover's lip"""
    tp_feature("Seam rebate", ring(pocket_x, z_cam, z_ant, REBATE_T, inset=0.01), PARTING - REBATE_H, PARTING + 0.1, False,
               f"{REBATE_T} x {REBATE_H} round the seam: the cover's lip, no wobble")
    fills = [((pocket_x - 0.01, z0, pocket_x + REBATE_T + 0.05, z1) if side > 0 else (-pocket_x - REBATE_T - 0.05, z0, -pocket_x + 0.01, z1))
             for side, z0, z1 in REBATE_FILL]
    tp_feature("Seam rebate fills", fills, PARTING - REBATE_H - 0.01, PARTING, True, f"{len(fills)}: flush where the cover has no lip")


def seam_screw_base_features(bottom, optional=False):
    tp_feature("Seam screw counterbores", [], bottom - 0.1, SCREW_SEAT, False, f"from the bottom to Y {SCREW_SEAT}",
               circles=[(x, z, SCREW_CB) for x, z in SEAM_SCREWS], optional=optional)
    tp_feature("Seam screw holes", [], SCREW_SEAT - 0.01, PARTING + 0.1, False, "clearance up through the base",
               circles=[(x, z, SCREW_HOLE) for x, z in SEAM_SCREWS], optional=optional)


def seam_screw_cover_features():
    top = SCREW_SEAT + SCREW_L + 0.7
    tp_feature("Seam screw pilots", [], PARTING - 0.1, top, False, f"thread-forming, Y {PARTING}..{top:.1f}",
               circles=[(x, z, SCREW_PILOT) for x, z in SEAM_SCREWS])


def seam_screws():
    """(axis rows, head-top point mm) of the four screws, for the assembly"""
    return [([0, 1, 0, 1, 0, 0, 0, 0, -1], (x, SCREW_SEAT - HEAD_H, z)) for x, z in SEAM_SCREWS]


# --- the probe wires' plugs: JST PHR-2 (SPH-002T-P0.5S crimps) in the S2B-PH-K-S side-entry headers S1-S3.
# Seated, a PHR-2 ends 8.26 past its header's pins (JST models: the pins' board legs 1.35 in front of
# the header's back, the plug's front on the header's stop 2.75 further, the plug 6.86 long), which is
# inside the old 1.1 side wall: its rear flange (5.8 wide, 4.5 high from 0.3 over the board back) sits
# in a pocket in the wall.
# The wires (standard crimped, flexible) run from the plug's rear in a groove in the base, open to the
# parting (laid in, the cover closes it), to a star of the outer form on the parting line, and out through
# a tube laid in its seat there (half in each part, clamped): the probe leg. form_model.py legs, cut here
# by legs_cut.
PLUGS = [(1, 10.972, -20.567), (1, 10.8, 17.5), (-1, -10.8, -1.5)]   # S1, S3, S2: wall side, pins X, Z middle
PLUG_REACH, PLUG_W, PLUG_Y = 8.26, 5.8, (1.895, 6.395)               # seated rear past the pins, flange, Y span
PLUG_CLEAR = 0.2


def plug_rear(pins_x, side):
    return pins_x + side * PLUG_REACH


def plug_pocket(side, pins_x, zc, outer_x):
    """XY of the flange pocket (side-signed X), Z span"""
    x1 = abs(plug_rear(pins_x, side)) + PLUG_CLEAR
    return (side * (outer_x - 1.2), side * x1), (zc - PLUG_W / 2 - PLUG_CLEAR, zc + PLUG_W / 2 + PLUG_CLEAR)


LIP_OFF = [(1, -22.9, 32.7)]      # no lip: +X beside the battery plate and S3: the cover is cut back to its band, the base
                                 # open or a 45 deg side fill there (fit 10-10c/d), up to the antenna-end seam wall
REBATE_FILL = LIP_OFF                            # the rebate filled flush wherever the cover has no lip


def plug_gaps(pocket_x):
    """the lip's gaps over the plugs, {side: [(z0, z1)]}"""
    out = {}
    for side, px, zc in PLUGS:
        out.setdefault(side, []).append((zc - PLUG_W / 2 - PLUG_CLEAR - 0.1, zc + PLUG_W / 2 + PLUG_CLEAR + 0.1))
    for side, z0, z1 in LIP_OFF:
        out.setdefault(side, []).append((z0, z1))
    for side, zs in out.items():                     # overlapping rectangles make a bad sketch: merged
        m = []
        for z0, z1 in sorted(zs):
            if m and z0 <= m[-1][1]:
                m[-1] = (m[-1][0], max(m[-1][1], z1))
            else:
                m.append((z0, z1))
        out[side] = m
    return out


def plug_features(part, outer_x):
    """part 'base' (below the seam) or 'cover' (above it)"""
    lo, hi = PLUG_Y[0] - PLUG_CLEAR, PLUG_Y[1] + PLUG_CLEAR
    for side, px, zc in PLUGS:
        nm = f"{'+X' if side > 0 else '-X'} Z {zc:+.1f}"
        (xa, xb), (z0, z1) = plug_pocket(side, px, zc, outer_x)
        ys = (lo, PARTING + 0.1) if part == "base" else (PARTING - 0.1, hi)
        fp_feature(f"Plug flange pocket {nm}", [(xa, ys[0]), (xb, ys[0]), (xb, ys[1]), (xa, ys[1])], z0, z1, False,
                   f"the PHR-2's rear to X {plug_rear(px, side):.2f}, {PLUG_CLEAR} clear")


# --- the outer form (form_model.py, frozen), split along its own edges: each part starts as its piece
# (<part>_outer.step: inside the old block the parting is the flat seam at PARTING); exits are cut out to
# SKIN_OUT, past its skin, and the part is trimmed to its piece at the end
FORM_STEP = r"C:\hw\LEAKCAM\mechanical\enclosure\{}_outer.step"   # the form's piece for each part
LEGS_STEP = r"C:\hw\LEAKCAM\mechanical\enclosure\legs_{}.step"   # the probe legs' wire channels and tube bores
LEDS_STEP = r"C:\hw\LEAKCAM\mechanical\enclosure\leds_{}.step"   # the LED strips' seats and LED holes
SHELL_STEP = r"C:\hw\LEAKCAM\mechanical\enclosure\shell_{}.step"   # the hollow (form_model.py): all but the skin and structures
SKIN_OUT = 40.0
BODY_SUBTRACT = 15902                                             # swBodyOperationType_e


def form_body(path):
    """a copy of the form's solid (or another STEP's) as a new body of this part"""
    title = get(model, "GetTitle")
    data = sw.GetImportFileData(path)
    e = byref_int()
    src = sw.LoadFile4(path, "r", data if data is not None else nothing(), e)
    if src is None:
        raise RuntimeError(f"import of {path} failed: {e.value}")
    bodies = [typed(b_, "IBody2").Copy() for b_ in src.GetBodies2(0, False)]
    sw.CloseDoc(get(src, "GetTitle"))
    sw.ActivateDoc3(title, False, 0, byref_int())
    for body in bodies:
        f = typed(model, "IPartDoc").CreateFeatureFromBody3(body, False, 0)
        if f is None:
            raise RuntimeError(f"body from {path} failed")
    return f


def form_start(part):
    """the part's piece of the form as its first body"""
    form_body(FORM_STEP.format(part)).Name = "Outer form piece"
    print(f"{'Outer form piece':26s} {[round(v * 1000, 2) for v in model.GetPartBox(True)]}")


def form_trim(part):
    """everything outside the form off (cut-outs and bosses made past the skin): a box body less a copy
    of the form is the outside, the part less the outside is the part (Combine's intersect does not run
    here, subtract does)"""
    import pythoncom, win32com.client
    which = part
    pd = typed(model, "IPartDoc")
    names = lambda: {get(typed(b_, "IBody2"), "Name"): b_ for b_ in pd.GetBodies2(0, False)}
    part = names()
    if len(part) != 1:
        raise RuntimeError(f"form trim: {len(part)} bodies before")
    form_body(FORM_STEP.format(which))
    form = {k: v for k, v in names().items() if k not in part}
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    sm.CreateCornerRectangle(-0.1, -0.1, 0, 0.1, 0.1, 0)
    sm.InsertSketch(True)
    bx = fm.FeatureExtrusion3(False, False, False, 0, 0, 0.1, 0.1, False, False, False, False, 0, 0,
                              False, False, False, False, False, True, True, 0, 0, False)     # not merged: own body
    if bx is None:
        raise RuntimeError("form trim: box failed")
    box = {k: v for k, v in names().items() if k not in part and k not in form}
    arr = lambda b_: win32com.client.VARIANT(pythoncom.VT_ARRAY | pythoncom.VT_DISPATCH, [b_])
    c1 = fm.InsertCombineFeature(BODY_SUBTRACT, list(box.values())[0], arr(list(form.values())[0]))
    if c1 is None:
        raise RuntimeError("form trim: outside failed")
    c1.Name = "Outside of the form"
    outside = [v for k, v in names().items() if k not in part][0]
    c2 = fm.InsertCombineFeature(BODY_SUBTRACT, list(part.values())[0], arr(outside))
    if c2 is None:
        raise RuntimeError("form trim failed")
    c2.Name = "Trimmed to the outer form"
    print("trimmed to the form:", len(pd.GetBodies2(0, False)), "body", [round(v * 1000, 2) for v in model.GetPartBox(True)])


def cut_bodies(path, name):
    """the bodies of a form_model.py STEP off the part"""
    import pythoncom, win32com.client
    pd = typed(model, "IPartDoc")
    names = lambda: {get(typed(b_, "IBody2"), "Name"): b_ for b_ in pd.GetBodies2(0, False)}
    main = names()
    form_body(path)
    tools = [v for k, v in names().items() if k not in main]
    f = fm.InsertCombineFeature(BODY_SUBTRACT, list(main.values())[0],
                                win32com.client.VARIANT(pythoncom.VT_ARRAY | pythoncom.VT_DISPATCH, tools))
    if f is None:
        raise RuntimeError(f"{name} failed")
    f.Name = name
    print(f"{name}: {len(tools)} off,", len(pd.GetBodies2(0, False)), "body left")


def legs_cut(part):
    """the probe legs (form_model.py legs_<part>.step): the wire channels and the tube's bore"""
    cut_bodies(LEGS_STEP.format(part), "Probe leg channels (form_model.py)")


def leds_cut(part):
    """the LED strips (form_model.py leds_<part>.step): each strip's seat in the skin, its LEDs' square holes"""
    if os.path.exists(LEDS_STEP.format(part)):
        cut_bodies(LEDS_STEP.format(part), "LED strip seats (form_model.py)")


def shell_cut(part):
    """the hollow off the part: form_model.py's shell_<part>.step (everything inside the 2 mm skin but a band
    along the parting, the simple structures of form_params.json keep_<part> and walls round the legs'
    channels)"""
    import pythoncom, win32com.client
    p = SHELL_STEP.format(part)
    if not os.path.exists(p):
        print("no shell yet:", p)
        return
    pd = typed(model, "IPartDoc")
    names = lambda: {get(typed(b_, "IBody2"), "Name"): b_ for b_ in pd.GetBodies2(0, False)}
    main = names()
    if len(main) != 1:
        raise RuntimeError(f"shell: {len(main)} bodies before")
    form_body(p)
    tools = [v for k, v in names().items() if k not in main]
    f = fm.InsertCombineFeature(BODY_SUBTRACT, list(main.values())[0],
                                win32com.client.VARIANT(pythoncom.VT_ARRAY | pythoncom.VT_DISPATCH, tools))
    if f is None:
        raise RuntimeError("shell cut failed")
    f.Name = "Shell: the hollow off (form_model.py)"
    bodies = pd.GetBodies2(0, False)
    if len(bodies) > 1:                        # bits the hollow cut loose (left between two cuts): off
        size = lambda b_: (lambda q: (q[3] - q[0]) * (q[4] - q[1]) * (q[5] - q[2]))(get(typed(b_, "IBody2"), "GetBodyBox"))
        loose = sorted(bodies, key=size)[:-1]
        model.ClearSelection2(True)
        for b_ in loose:
            print("  loose piece removed:", [round(v * 1000, 2) for v in get(typed(b_, "IBody2"), "GetBodyBox")])
            typed(b_, "IBody2").Select2(True, None)
        d = fm.InsertDeleteBody2(False)
        if d:
            d.Name = "Loose bits of the shell removed"
    print(f"shell: {len(tools)} hollow bodies off,", len(pd.GetBodies2(0, False)), "body left")
