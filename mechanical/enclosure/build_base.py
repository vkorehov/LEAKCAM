"""Build mechanical/enclosure/base.SLDPRT: the ABS base holding the LEAKCAM main board and the
bottom camera. Run on the SolidWorks PC, after clips.py (seam lip, seam screws), through the solidworks-mcp run_python tool (it provides
sw, get, nothing, byref_int).

Base coordinates (mm, SolidWorks works in metres):
  main board (MIFA.step x, y, z) -> X = x - 42.25, Y = -z, Z = y - 32.5
    the board's top layer and USB-C connectors face the base (-Y); board top face at Y 0, back at 1.6;
    camera end (y 0) at -Z, antenna end (y 65) at +Z
  camera (Arducam_B0370 x, y, z) -> X = -x, Y = z + CAM_BACK, Z = y + 1.1 + CAM_Z
    lens axis at (0, CAM_Z), the middle of the base; the lens screws into the holder from outside
Top Plane sketch (x, y) = model (X, -Z). Every feature's face box is printed next to what it should be.

Assembly: camera into its pocket, four M2 screws from its back into the floor; flex from the camera connector along the floor to the camera end, 180 degree loop up into J5
(the J4 flex shares that shaft); the board goes straight down, the cell cradle on it, fixed together
by one M2x10 screw (Ø2.6 hole; S2 is 2.19 mm from its centre, so the head is at most Ø4: ISO 7380 M2,
Ø3.5); the cover's ribs hold the board's antenna end down.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\enclosure\base.SLDPRT"

PARTING = 4.7             # top face, where the cover sits: the probe wires' centre height
OUTER_X = 19.55           # side faces: 0.8 mm in front of the USB-C receptacles (MacBook-style ports)
POCKET_X = 18.45          # board pocket: 0.2 each side of the 36.5 board
END_CAM = -39.5           # block end at the camera end: room for the flex loop (cavity to LOOP_END)
END_ANT = 37.3            # block end at the antenna end
LOOP_END = -37.5
LOOP_FLOOR, FLEX_RAMP = -9.5, (-30.0, -17.9)          # the flex loop's floor (the form is low at the end), ramp Z
CAM_Z = (END_CAM + END_ANT) / 2                        # camera centred in the base: -1.1
CAM_BACK = -4.8           # camera board back: 1.55 below USBC1 (Y -3.25), screw heads 0.45 under it; the form's crater
                          # is round the lens there (form_model lens_raise_mm 0); estimated lens, re-measure
CAM_T = 2.38232           # camera board incl. parts (model, exact: it rests on the ledge)
FLANGE = 3.0              # M12 holder flange
BARREL = 2.2              # holder barrel below the flange
FLOOR = 1.82              # under the barrel
BOTTOM = CAM_BACK - CAM_T - FLANGE - BARREL - FLOOR   # -18.2: the old floor under the barrel
LEDGE = CAM_BACK - CAM_T                               # camera board lens-side face rests here
BARREL_END = LEDGE - FLANGE - BARREL
LENS_THREAD = 12.5 / 2    # M12 x 0.5 lens thread clearance
LENS_BORE = 8.1           # past the old floor: round the lens body (Ø15), up to the crater
CAM_PILOT = 3.5                                         # camera screw pilot depth: M2x5 through the 2.4 board leaves 2.6 of
                                                        # thread, 0.9 spare; >= 1 mm of plastic past the tip
CAM_SCREWS = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]   # camera x, y of its Ø2.2 holes
BOARD_HOLE = (-15.64, -6.64)                           # main board Ø2.6 hole (26.61, 25.86)
RIB_IN = 3.0              # ribs reach 3 mm in under the board
RIBS = {-1: [0.55, 58.0], 1: [0.65, 59.5]}      # board y of the 2 mm ribs, per long edge (free
                                                       # of top-side parts)
# USB-C receptacles (TYPE-C-31-M-12): wall side, mouth centre Z, front face X. Each sits in a notch in
# the pocket wall (open upwards: the board goes straight down), its front
# 0.05 from a 0.8 mm skin with a stadium like its mouth; the plug's overmold stops on the flat side
USB = [(-1, -14.30, -18.73), (1, -12.06, 18.61)]         # USBC2 (x 23.52), USBC1 (x 60.86)
USB_Y = -1.65
PLUG_OVERMOLD = (13.1 + 0.8, 6.7 + 0.8)                 # USB-C cable plug overmold (usb/USB_Type-C_Plug), free fit


def cz(y):                # camera y -> base Z
    return y + 1.1 + CAM_Z


for d in get(sw, "GetDocuments") or ():
    if get(d, "GetTitle").lower().startswith("base"):
        sw.CloseDoc(get(d, "GetTitle"))
model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension


def sketch(rects=(), circles=()):
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for x0, z0, x1, z1 in rects:
        sm.CreateCornerRectangle(x0 * M, -z0 * M, 0, x1 * M, -z1 * M, 0)
    for x, z, r in circles:
        sm.CreateCircleByRadius(x * M, -z * M, 0, r * M)
    sm.InsertSketch(True)


def boss(sd, down, d1, d2=0):        # Dir (third) False: first direction +Y
    return fm.FeatureExtrusion3(sd, False, down, 0, 0, d1 * M, d2 * M, False, False, False, False, 0, 0,
                                False, False, False, False, True, True, True, 0, 0, False)


def cut(sd, t1, d1, t2=0, d2=0, up=False, t0=0, start=0, flip_start=False):   # first direction -Y
    return fm.FeatureCut4(sd, False, up, t1, t2, d1 * M, d2 * M, False, False, False, False, 0, 0,
                          False, False, False, False, False, True, True, False, False, False,
                          t0, start * M, flip_start, False)


def down(depth):                    # from Y 0 down to -depth (above Y 0 is open pocket there)
    return cut(True, 0, depth)


def below(y):                       # from Y y (< 0) down through the floor
    return cut(True, 1, 0, t0=3, start=-y, flip_start=True)


def span(y0, y1=None):              # cut from Y y0 up to y1, or up through the top
    t1, d = (1, 0) if y1 is None else (0, y1 - y0)
    return fm.FeatureCut4(True, False, True, t1, 0, d * M, 0, False, False, False, False, 0, 0,
                          False, False, False, False, False, True, True, False, False, False,
                          3, y0 * M, False, False)


def rrect(z, y, wz, hy, r):         # Right Plane profile: sketch (u, v) = model (-Z, Y)
    u, v, a, b = -z, y, wz / 2, hy / 2
    r = min(r, a, b)
    L = lambda p, q: sm.CreateLine(p[0] * M, p[1] * M, 0, q[0] * M, q[1] * M, 0)
    A = lambda c, p, q: sm.CreateArc(c[0] * M, c[1] * M, 0, p[0] * M, p[1] * M, 0, q[0] * M, q[1] * M, 0, 1)
    if a - r > 1e-6:
        L((u - a + r, v - b), (u + a - r, v - b)); L((u + a - r, v + b), (u - a + r, v + b))
    if b - r > 1e-6:
        L((u + a, v - b + r), (u + a, v + b - r)); L((u - a, v + b - r), (u - a, v - b + r))
    A((u + a - r, v - b + r), (u + a - r, v - b), (u + a, v - b + r))
    A((u + a - r, v + b - r), (u + a, v + b - r), (u + a - r, v + b))
    A((u - a + r, v + b - r), (u - a + r, v + b), (u - a, v + b - r))
    A((u - a + r, v - b + r), (u - a, v - b + r), (u - a + r, v - b))


def side_cut(name, profile, x0, x1, expect):     # a profile on the Right Plane, cut through X x0..x1
    model.ClearSelection2(True)
    assert ext.SelectByID2("Right Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    profile()
    sm.InsertSketch(True)
    if x0 >= 0:
        up, start, flip = True, x0, False
    else:
        up, start, flip = False, -x1, True
    f = fm.FeatureCut4(True, False, up, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                       False, False, False, False, False, True, True, False, False, False,
                       3, start * M, flip, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def fbox(f):
    bs = [[round(v * 1000, 2) for v in get(face, "GetBox")] for face in (get(f, "GetFaces") or ())]
    return [min(b[i] for b in bs) for i in range(3)] + [max(b[i] for b in bs) for i in range(3, 6)]


def feature(name, geo, make, expect):
    sketch(**geo)
    f = make()
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


ribs = []
for side, ys in RIBS.items():
    for y in ys:
        z0, z1 = max(y - 32.5 - 1.0, -32.8), y - 32.5 + 1.0                # 2 mm: no thin standing ribs
        ribs.append((-POCKET_X, z0, -18.25 + RIB_IN, z1) if side < 0 else (18.25 - RIB_IN, z0, POCKET_X, z1))

sm.AddToDB = True
form_start("base")
feature("Board pocket", dict(rects=[(-POCKET_X, -32.8, POCKET_X, 32.8)]), lambda: cut(False, 0, 4.0, 1, 0),
        f"X +-{POCKET_X} Y -4..{PARTING} Z +-32.8")
feature("USB-C receptacle notches", dict(rects=[(front - 0.05, zc - 4.6, -POCKET_X + 0.01, zc + 4.6) if s < 0 else
                                                 (POCKET_X - 0.01, zc - 4.6, front + 0.05, zc + 4.6) for s, zc, front in USB]),
        lambda: span(-3.4), "fronts 0.05 from the skin, open upwards for the board going in")
for s, zc, front in USB:
    face = front + 0.05 * s
    side_cut(f"USB-C mouth {'USBC2' if s < 0 else 'USBC1'}", lambda: rrect(zc, USB_Y, 9.1, 3.5, 1.75),
             *sorted((face - 0.3 * s, (OUTER_X + 0.1) * s)), f"stadium 9.1 x 3.5 at Z {zc} Y {USB_Y}, old wall {OUTER_X - abs(face):.2f}")
    side_cut(f"USB-C plug way {'USBC2' if s < 0 else 'USBC1'}", lambda: rrect(zc, USB_Y, PLUG_OVERMOLD[0], PLUG_OVERMOLD[1], PLUG_OVERMOLD[1] / 2),
             *sorted((OUTER_X * s, SKIN_OUT * s)), f"the cable plug's overmold in from the skin to its stop at X {OUTER_X}")
f = top_span(rects=[(-8.73, LOOP_END, 7.27, -32.5)], ya=-9.5, yb=PARTING + 0.1)   # inside: the end wall stays closed
f.Name = "J4 flex notch"
print(f"{f.Name:26s} {fbox(f)}  expect Y -9.5..{PARTING}: one shaft from the flex loop up to the cover's J4 riser")
feature("Camera board pocket", dict(rects=[(-12.8, cz(-17.2), 12.8, cz(7.3))]), lambda: down(-LEDGE),
        f"X +-12.8 Y {LEDGE:.2f}")
feature("Holder pocket", dict(rects=[(-7.3, CAM_Z - 6.8, 7.3, CAM_Z + 6.8)]), lambda: down(-(LEDGE - FLANGE)),
        f"X +-7.3 Y {LEDGE - FLANGE:.2f}")
feature("Holder ear pocket", dict(rects=[(-11.3, CAM_Z - 2.3, 11.3, CAM_Z + 2.3)]), lambda: down(-(LEDGE - FLANGE)),
        f"X +-11.3 Y {LEDGE - FLANGE:.2f}")
feature("Camera FFC pocket", dict(rects=[(-9.1, cz(-17.9), 9.1, cz(-12.1))]), lambda: down(-(CAM_BACK - 4.39) + 0.3),
        f"Y {CAM_BACK - 4.69:.2f}")
feature("Flex channel and loop", dict(rects=[(-9.1, LOOP_END, 8.4, FLEX_RAMP[0])]), lambda: down(-LOOP_FLOOR),
        f"Y {LOOP_FLOOR} Z {LOOP_END}..{FLEX_RAMP[0]}")
rp_feature("Flex channel ramp", [(FLEX_RAMP[0], 0.1), (FLEX_RAMP[0], LOOP_FLOOR), (FLEX_RAMP[1], CAM_BACK - 4.69),
                                 (cz(-16.6), CAM_BACK - 4.69), (cz(-16.6), 0.1)], -9.1, 8.4, False,
           f"down to the camera's connector, Y {CAM_BACK - 4.69:.2f} at Z {FLEX_RAMP[1]}")
feature("Barrel hole", dict(circles=[(0, CAM_Z, 7.3)]), lambda: down(-BARREL_END), f"R 7.3 Y {BARREL_END:.2f}")
feature("Lens thread hole", dict(circles=[(0, CAM_Z, LENS_THREAD)]), lambda: below(BARREL_END),
        f"R {LENS_THREAD} Y ..{BARREL_END:.2f}")
feature("Lens bore", dict(circles=[(0, CAM_Z, LENS_BORE)]), lambda: below(BOTTOM), f"R {LENS_BORE} from Y {BOTTOM:.2f} out: the lens body")
feature("Camera screw pilots", dict(circles=[(-x, cz(y), 0.8) for x, y in CAM_SCREWS]), lambda: down(-(LEDGE - CAM_PILOT)),
        f"4x Ø1.6 to Y {LEDGE - CAM_PILOT:.2f} (M2x5 thread-forming, button head)")
feature("Board support pad", dict(rects=[(8.55, -32.8, 16.25, -30.8)]), lambda: boss(True, True, 4.0),
        "X 8.55..16.25 Y -4..0 Z -32.8..-31.1")
feature("Board ribs", dict(rects=ribs), lambda: boss(True, True, 4.0), f"{len(ribs)} ribs Y -4..0")
seam_base_features(POCKET_X, *SEAM_Z)
plug_features("base", OUTER_X)
seam_screw_base_features(-SKIN_OUT)
# board supports on the floor (Y -4): the USB-C receptacles' shell pads, the board screw's boss and its
# pilot, a rib at board y 38.9
CX0 = 13.37
for nm, r in (("USBC1 shell pad", (CX0, -15.2, 17.25, -7.9)), ("USBC2 shell pad", (-17.25, -17.4, -CX0, -10.1))):
    f = top_span(rects=[r], ya=-4.01, yb=-3.25, boss=True); f.Name = nm
    print(f"{nm:26s} {fbox(f)}")
RIB_Z = 38.9 - 32.5
f = top_span(rects=[(-POCKET_X, RIB_Z - 1.0, -15.25, RIB_Z + 1.0)], ya=-4.01, yb=0.0, boss=True); f.Name = "Board rib, y 38.9"   # 2 mm, from the side
f = top_span(rects=[(-POCKET_X, BOARD_HOLE[1] - 2.0, BOARD_HOLE[0] + 2.0, BOARD_HOLE[1] + 2.0)], ya=-4.01, yb=0.0, boss=True)
f.Name = "Board screw boss"                             # square, out to the pocket's side (prints from the wall)
f = top_span(circles=[(*BOARD_HOLE, 0.8)], ya=-3.5, yb=0.1); f.Name = "Board screw pilot"     # its block starts at -4.5
print(f"{'Board screw boss':26s} {fbox(f)}  M2x8 thread-forming through the cradle and the board")
form_trim("base")
legs_cut("base")
leds_cut("base")
shell_cut("base")
sm.AddToDB = False

print("part box:", [round(v * 1000, 2) for v in model.GetPartBox(True)])
err, warn = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), err, warn), err.value)

