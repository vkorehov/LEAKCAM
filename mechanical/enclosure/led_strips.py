"""LED strip geometry shared by build_base.py and build_cover.py (prepended to either when it runs:
led_strips.py + clips.py + build_*.py): two strips per camera on 30 degree chamfers along the long
edges, the base's mirroring the cover's about the plane between the two cameras. Base coordinates
as in build_base.py.

A strip (LEDS.step: 5 x 37 x 1.6, LEDs W IR IR W at 3, 8, 30, 35 mm) lies flat against the inside
of a wall as thin as the LED cube (0.76); each LED's 3.5 mm cube passes a square hole, flush, the
dome stands out. LEDs centred on the camera axis, symmetric about the cameras.

Fitting, the same way in as the cameras: the strip goes straight down its shaft from the open side
to just short of the wall, then out along its normal so the cubes drop into their holes. A printed
clamp (clamp_*.SLDPRT, one per strip), a straight bar narrower than the strip, slides down behind
it; its end is cut to the strip's back, so its two M2 screws, at the clamp's ends beyond the strip,
press the strip into place. Glue on top.
The base clamps carry the board supports that stood over their shafts (the USB-C pads, the board
screw boss, one rib); the cover clamps press on the upper cell.
"""
import math

TILT = 30.0                   # strip normal from the camera axis, outwards (specular glare)
CHAMFER = 8.5                 # chamfer face width along the slope
STRIP_W, STRIP_T, STRIP_L = 5.0, 1.6, 37.0
LED_H, LED_BODY = 2.1, 3.5
LED_CLEAR = 0.05              # per side around the LED body (the model's are up to 3.51)
LEDS_AT = (3.0, 8.0, 30.0, 35.0)     # along the strip from its camera-end edge
STRIP_WALL = 0.76             # wall in front of the strip = the LED cube's height: cube flush, the whole dome out
STRIP_SHIFT = 0.8             # strip centre from the chamfer centre towards the side face
LEDS_ZC = -1.1                # the LEDs' centre (19 mm along the strip) on the camera axis: symmetric
STRIP_Z0 = LEDS_ZC - (LEDS_AT[0] + LEDS_AT[-1]) / 2
STRIP_Z1 = STRIP_Z0 + STRIP_L                   # board y 12.4 .. 49.4
CUBE = 0.76                   # LED cube height over the strip
PUSH = CUBE + 0.1             # the strip comes down this far short of the wall, then goes out
CLAMP_CLEAR = 0.05
EAR_L, EAR_D = 6.0, 2.5       # clamp ears beyond the strip ends: length, depth into the part
SCREW_X = 15.35               # clamp screws (|X|), M2x6 thread-forming, heads in counterbores
SCREW_PILOT, SCREW_HOLE, SCREW_HEAD, SCREW_CB = 0.8, 1.1, 2.0, 1.2      # radii, counterbore depth
PADS_AT = (16.01, 17.63)      # the strip's wire pads (two 2-pin THT pads, J3/J6..J11/J12, 1.5 either side of
                              # its centre line) along it from the camera-end edge: the wires leave from its back
WIRE_CHANNEL = 1.6            # depth of the channel across the clamp's pressing face over the pads
WIRE_Z = (STRIP_Z0 + PADS_AT[0] - 0.9, STRIP_Z0 + PADS_AT[1] + 0.9)
WIRE_NOTCH = 1.3              # at the clamp's outer edge, from the channel through to its open face


def strip_frame(side, v, corner_y, outer_x):
    """(point(s, d), t, n, p1) for the chamfer on wall `side` (-1/+1) at the top (v = +1, corner Y
    corner_y) or bottom (v = -1) edge: s along the face (0 = strip centre), d depth into the part"""
    c, s_ = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
    t = (side * c, -v * s_)                       # along the face, from the top/bottom face to the side
    n = (side * s_, v * c)                        # outward normal
    p1 = (side * (outer_x - CHAMFER * c), corner_y)
    mid = (p1[0] + (CHAMFER / 2 + STRIP_SHIFT) * t[0], p1[1] + (CHAMFER / 2 + STRIP_SHIFT) * t[1])

    def point(sa, d):
        return (mid[0] + sa * t[0] - d * n[0], mid[1] + sa * t[1] - d * n[1])
    return point, t, n, p1


def quad(point, s0, s1, d0, d1):
    return [point(s0, d0), point(s1, d0), point(s1, d1), point(s0, d1)]


def hull(pts):
    pts = sorted(set((round(x, 6), round(y, 6)) for x, y in pts))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]                      # counter-clockwise


def shaft(side, v, corner_y, outer_x, cavity_y):
    """the strip's way in, two XY polygons: straight down from the open side (Y cavity_y) to where it
    stops PUSH short of the wall, cubes included (vertical walls), and the push out to its seat"""
    point = strip_frame(side, v, corner_y, outer_x)[0]
    w = STRIP_W / 2 + 0.1
    seated = [point(s, d) for s in (-w, w) for d in (STRIP_WALL, STRIP_WALL + STRIP_T)]
    pre = [point(s, d + PUSH) for s in (-w, w) for d in (STRIP_WALL, STRIP_WALL + STRIP_T)]
    tips = [point(s, STRIP_WALL + PUSH - CUBE - 0.05) for s in (-LED_BODY / 2 - 0.1, LED_BODY / 2 + 0.1)]
    down = hull(pre + tips + [(x, cavity_y) for x, _ in pre + tips])
    return down, hull(seated + pre + tips)


def strip_back_y(side, v, corner_y, outer_x, x):
    """Y of the seated strip's back face at X x"""
    point, t, n, p1 = strip_frame(side, v, corner_y, outer_x)
    x0, y0 = point(0.0, STRIP_WALL + STRIP_T)
    return y0 + (x - x0) * t[1] / t[0]


def clamp_x(side, v, corner_y, outer_x):
    """the clamp's X range: straight sides, 0.15 inside the strip's back (it need not cover all of it)"""
    point = strip_frame(side, v, corner_y, outer_x)[0]
    xs = sorted(point(s, STRIP_WALL + STRIP_T)[0] for s in (-STRIP_W / 2, STRIP_W / 2))
    return xs[0] + 0.15, xs[1] - 0.15


def clamp_outline(side, v, corner_y, outer_x, cavity_y, overreach):
    """the clamp's section: a straight bar from its open face (cavity_y, `overreach` past it out of
    the part) to the strip's back, its end cut to the back's slope so it presses flat on it"""
    xa, xb = clamp_x(side, v, corner_y, outer_x)
    face = cavity_y - v * overreach
    return [(xa, face), (xb, face), (xb, strip_back_y(side, v, corner_y, outer_x, xb)),
            (xa, strip_back_y(side, v, corner_y, outer_x, xa))]


def ear_rect(side, cavity_y, v, clear):
    """an ear's XY rectangle, EAR_D into the part from cavity_y"""
    x0, x1 = 12.7 + clear, 18.0 - clear
    y0, y1 = sorted((cavity_y, cavity_y + v * (EAR_D - clear)))
    return ((x0, x1) if side > 0 else (-x1, -x0)), (y0, y1)


def rect(x, y):
    return [(x[0], y[0]), (x[1], y[0]), (x[1], y[1]), (x[0], y[1])]


def fp_cut(name, pts, z0, z1, expect, boss=False):
    """a polygon (X, Y) on the Front Plane (sketch u, v = model X, Y; normal +Z) cut or bossed
    through Z z0..z1 (uses the build script's model, sm, fm, ext, fbox)"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Front Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for (a, b), (c, d) in zip(pts, pts[1:] + pts[:1]):
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


def top_poly(pts, ya, yb):
    """a polygon (x, z) on the Top Plane, cut over Y ya..yb"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for (a, b), (c, d) in zip(pts, pts[1:] + pts[:1]):
        sm.CreateLine(a * M, -b * M, 0, c * M, -d * M, 0)
    sm.InsertSketch(True)
    return fm.FeatureCut4(True, False, True, 0, 0, (yb - ya) * M, 0, False, False, False, False, 0, 0,
                          False, False, False, False, False, True, True, False, False, False,
                          3, abs(ya) * M, ya < 0, False)


def screw_zs():
    return (STRIP_Z0 - 0.1 - EAR_L / 2, STRIP_Z1 + 0.1 + EAR_L / 2)


def led_strip_features(v, corner_y, outer_x, end_cam, end_ant, cavity_y):
    """the chamfers, strip shafts, clamp ear pockets, clamp screw pilots and LED holes of one part"""
    for side in (-1, 1):
        point, t, n, p1 = strip_frame(side, v, corner_y, outer_x)
        corner = (side * (outer_x + 1.0), corner_y + v * 1.0)
        p2 = (side * outer_x, corner_y - v * CHAMFER * math.sin(math.radians(TILT)))
        tri = [(p1[0] - t[0], p1[1] - t[1]), corner, (p2[0] + t[0], p2[1] + t[1])]      # the face, 1 mm longer each way
        name = f"{'+X' if side > 0 else '-X'} {'top' if v > 0 else 'bottom'}"
        fp_cut(f"Chamfer {name}", tri, end_cam - 1, end_ant + 1, f"{TILT:.0f} deg, {CHAMFER} wide")
        down, push = shaft(side, v, corner_y, outer_x, cavity_y)
        fp_cut(f"LED strip shaft {name}", down, STRIP_Z0 - 0.1, STRIP_Z1 + 0.1, f"straight down from Y {cavity_y}")
        fp_cut(f"LED strip seat {name}", push, STRIP_Z0 - 0.1, STRIP_Z1 + 0.1, "the push out to the wall")
        ex, ey = ear_rect(side, cavity_y, v, 0.0)
        for z0, z1 in ((STRIP_Z0 - 0.1 - EAR_L, STRIP_Z0 - 0.1), (STRIP_Z1 + 0.1, STRIP_Z1 + 0.1 + EAR_L)):
            fp_cut(f"Clamp ear pocket {name} {z0:.0f}", rect(ex, ey), z0, z1, f"{EAR_D} deep")
        y0, y1 = sorted((cavity_y + v * EAR_D, cavity_y + v * (EAR_D + 5.0)))
        f = top_span(circles=[(side * SCREW_X, z, SCREW_PILOT) for z in screw_zs()], ya=y0, yb=y1)
        f.Name = f"Clamp screw pilots {name}"
        print(f"{f.Name:26s} {fbox(f)}  expect Ø{2 * SCREW_PILOT} Y {y0:.2f}..{y1:.2f}")
        for z in LEDS_AT:
            zc = STRIP_Z0 + z
            h = LED_BODY / 2 + LED_CLEAR
            fp_cut(f"LED hole {name} {z:g}", quad(point, -h, h, -0.3, STRIP_WALL + 0.05), zc - h, zc + h,
                   f"{2 * h:.1f} square at Z {zc:.1f}")


def build_clamp(path, side, v, corner_y, outer_x, cavity_y, overreach, bosses=(), round_bosses=(), holes=(),
                profiles=(), wire_z=(WIRE_Z,)):
    """one strip's clamp, in base coordinates (placed as is): the wedge behind the strip, ears with
    counterbored screw holes, a channel across its pressing face over the strip's wire pads (the wires
    run along it and out through a notch at the outer edge, `wire_z` its Z spans); `bosses` (x0, z0, x1,
    z1, y0, y1), `round_bosses` and `holes` (x, z, r, y0, y1), `profiles` (XY polygon, z0, z1) for the
    board supports it carries.

    Printed lying on its inner side face (the one towards the part's middle): that face is one plane,
    the ears flush with it, the counterbores open to it, whatever the clamp carries reaching down to it."""
    global model, sm, fm, ext
    keep = model, sm, fm, ext
    model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
    sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension
    sm.AddToDB = True
    nm = f"{'+X' if side > 0 else '-X'} {'cover' if v > 0 else 'base'}"
    fp_cut(f"Clamp {nm}", clamp_outline(side, v, corner_y, outer_x, cavity_y, overreach),
           STRIP_Z0 - 0.05, STRIP_Z1 + 0.05, "behind the strip", boss=True)
    point = strip_frame(side, v, corner_y, outer_x)[0]
    back = STRIP_WALL + STRIP_T
    for z0, z1 in wire_z:
        fp_cut(f"Wire channel {nm} Z {z0:.1f}", quad(point, -STRIP_W, STRIP_W, back - 0.1, back + WIRE_CHANNEL), z0, z1,
               f"{WIRE_CHANNEL} deep over the pads, open at both sides")
        xa, xb = clamp_x(side, v, corner_y, outer_x)
        nx = (xb - WIRE_NOTCH, xb + 0.3) if side > 0 else (xa - 0.3, xa + WIRE_NOTCH)
        face = cavity_y - v * overreach
        far = strip_back_y(side, v, corner_y, outer_x, nx[0 if side > 0 else 1]) - v * 1.0
        fp_cut(f"Wire notch {nm} Z {z0:.1f}", rect(nx, sorted((face - v * 0.1, far))), z0 + 0.15, z1 - 0.15,
               f"{WIRE_NOTCH} at the outer edge: the wires out through the open face")
    ex, (y0, y1) = ear_rect(side, cavity_y, v, CLAMP_CLEAR)
    xa, xb = clamp_x(side, v, corner_y, outer_x)
    inner = xa if side > 0 else xb                            # the print face
    ex = (inner, ex[1]) if side > 0 else (ex[0], inner)       # ears flush with it
    y0, y1 = (y0, cavity_y) if v < 0 else (cavity_y, y1)      # ears flush with the open side (only the wedge reaches past)
    face = cavity_y
    for z0, z1 in ((STRIP_Z0 - 0.05 - EAR_L + CLAMP_CLEAR, STRIP_Z0 - 0.05), (STRIP_Z1 + 0.05, STRIP_Z1 + 0.05 + EAR_L - CLAMP_CLEAR)):
        fp_cut(f"Ear {nm} {z0:.0f}", rect(ex, (y0, y1)), z0, z1, "", boss=True)
    screws = [(side * SCREW_X, z) for z in screw_zs()]
    top_span(circles=[(x, z, SCREW_HOLE) for x, z in screws], ya=y0 - 0.1, yb=y1 + 0.1)
    cb = dict(ya=min(face, face + v * SCREW_CB) - 0.01, yb=max(face, face + v * SCREW_CB) + 0.01)
    # the counterbores: open to the print face (no sliver wall), their far side a teardrop (45 deg
    # sides, cut flat 0.6 inside the ear's outer face) so lying on its side the clamp prints them
    r, cap = SCREW_HEAD, side * (18.0 - CLAMP_CLEAR - 0.6)
    q = r / math.sqrt(2)
    tear = [[(x, z - r), (x + side * q, z - q), (cap, z - (r * math.sqrt(2) - abs(cap - x))),
             (cap, z + (r * math.sqrt(2) - abs(cap - x))), (x + side * q, z + q), (x, z + r)] for x, z in screws]
    for f in [top_span(circles=[(x, z, SCREW_HEAD) for x, z in screws], **cb),
              top_span(rects=[((inner - 0.1, z - SCREW_HEAD, x, z + SCREW_HEAD) if side > 0 else
                               (x, z - SCREW_HEAD, inner + 0.1, z + SCREW_HEAD)) for x, z in screws], **cb)] + \
             [top_poly(t, cb["ya"], cb["yb"]) for t in tear]:
        if f is None:                                         # (one sketch with all of them would not cut)
            raise RuntimeError(f"clamp {nm}: counterbore failed")
    for x0, z0, x1, z1, ya, yb in bosses:
        top_span(rects=[(x0, z0, x1, z1)], ya=ya, yb=yb, boss=True)
    for x, z, r, ya, yb in round_bosses:
        top_span(circles=[(x, z, r)], ya=ya, yb=yb, boss=True)
    for pts, z0, z1 in profiles:
        fp_cut(f"Support {nm} Z {z0:.1f}", pts, z0, z1, "", boss=True)
    for x, z, r, ya, yb in holes:
        top_span(circles=[(x, z, r)], ya=ya, yb=yb)
    sm.AddToDB = False
    print(f"clamp {nm} box", [round(q * 1000, 2) for q in model.GetPartBox(True)])
    e, w = byref_int(), byref_int()
    print("saved:", ext.SaveAs(path, 0, 1, nothing(), e, w), e.value)
    sw.CloseDoc(get(model, "GetTitle"))
    model, sm, fm, ext = keep


def strip_transform(side, v, corner_y, outer_x):
    """rotation rows and translation (mm) placing LEDS.SLDPRT (board x 6..11, y 28..65, LEDs up +z)
    in its place, LED side against the wall"""
    point, t, n, p1 = strip_frame(side, v, corner_y, outer_x)
    rx = (-n[1], n[0], 0.0)                       # strip x (across) = (strip y) x (strip z)
    rows = [rx, (0.0, 0.0, 1.0), (n[0], n[1], 0.0)]
    target = point(0.0, STRIP_WALL) + (STRIP_Z0,)
    origin = (8.5, 28.0, 0.0)                     # strip centre line, camera end, LED-side face
    moved = [sum(origin[i] * rows[i][j] for i in range(3)) for j in range(3)]
    return [x for r in rows for x in r], [target[j] - moved[j] for j in range(3)]


def clamp_screws(side, v, face_y):
    """(axis rows, head-top point mm) of one clamp's two M2x6 screws, heads in the counterbores of
    the ears, flush with the open side face_y, for the assembly"""
    rows = [0, -1, 0, 1, 0, 0, 0, 0, 1] if v < 0 else [0, 1, 0, 1, 0, 0, 0, 0, -1]
    y = face_y - 0.1 if v < 0 else face_y + 0.1
    return [(rows, (side * SCREW_X, y, z)) for z in screw_zs()]
