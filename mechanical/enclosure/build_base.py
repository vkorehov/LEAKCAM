"""Build mechanical/enclosure/base.SLDPRT: the ABS base holding the LEAKCAM main board and the
bottom camera. Run on the SolidWorks PC through the solidworks-mcp run_python tool (it provides
sw, get, nothing, byref_int).

Base coordinates (mm, SolidWorks works in metres):
  main board (MIFA.step x, y, z) -> X = x - 42.25, Y = -z, Z = y - 32.5
    the board's top layer and USB-C connectors face the base (-Y); board top face at Y 0, back at 1.6;
    camera end (y 0) at -Z, antenna end (y 65) at +Z
  camera (Arducam_B0370 x, y, z) -> X = -x, Y = z + CAM_BACK, Z = y + 1.1 + CAM_Z
    lens axis at (0, CAM_Z), the middle of the base; the lens screws into the holder from outside
Top Plane sketch (x, y) = model (X, -Z). Every feature's face box is printed next to what it should be.

Assembly: camera into its pocket, four M2 screws from its back into the floor; flex from the camera
connector along the floor to the camera end, 180 degree loop up into J5; the board goes in tilted,
antenna end under the lip, lowered, slid back 1 mm (SLIDE) and fixed by one M2 screw (Ø2.6 hole;
S2 is 2.19 mm from its centre, so the head is at most Ø4: ISO 7380 M2, Ø3.5).
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\enclosure\base.SLDPRT"

END_CAM = -39.5           # block end at the camera end: room for the flex loop (cavity to LOOP_END)
END_ANT = 37.3            # block end at the antenna end
LOOP_END = -37.5
CAM_Z = (END_CAM + END_ANT) / 2                        # camera centred in the base: -1.1
CAM_BACK = -4.8           # camera board back: 1.55 below USBC1 (Y -3.25) for an M2 button head
CAM_T = 2.38232           # camera board incl. parts (model, exact: it rests on the ledge)
FLANGE = 3.0              # M12 holder flange
BARREL = 2.2              # holder barrel below the flange
FLOOR = 1.82              # under the barrel
BOTTOM = CAM_BACK - CAM_T - FLANGE - BARREL - FLOOR   # -14.2
LEDGE = CAM_BACK - CAM_T                               # camera board lens-side face rests here
BARREL_END = LEDGE - FLANGE - BARREL
LENS_THREAD = 12.5 / 2    # M12 x 0.5 lens thread clearance
CAM_SCREWS = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]   # camera x, y of its Ø2.2 holes
BOARD_HOLE = (-15.64, -6.64)                           # main board Ø2.6 hole (26.61, 25.86)
SLIDE = 1.0               # board travel under the lip while it goes in
LIP = 31.8                # lip over the antenna end of the board (edge at 32.5): 0.7 overlap
RIB_IN = 3.0              # ribs reach 3 mm in under the board
RIBS = {-1: [0.55, 38.9, 58.0], 1: [0.65, 59.5]}      # board y of the 1 mm ribs, per long edge (free
                                                       # of top-side parts incl. the SLIDE sweep)


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
        z0, z1 = max(y - 32.5 - 0.5, -32.8), y - 32.5 + 0.5
        ribs.append((-18.85, z0, -18.25 + RIB_IN, z1) if side < 0 else (18.25 - RIB_IN, z0, 18.85, z1))
USB_PADS = [(11.75, -15.2, 17.25, -7.9),      # under the USBC1 shell (x 52.96-60.86, y 15.97-24.91)
            (-17.25, -17.4, -13.3, -10.1)]    # under the USBC2 shell (x 23.52-31.42, y 13.73-22.67), off the camera pocket

sm.AddToDB = True
feature("Block", dict(rects=[(-20.85, END_CAM, 20.85, END_ANT)]), lambda: boss(False, False, 3.1, -BOTTOM),
        f"X +-20.85 Y {BOTTOM:.2f}..3.1 Z {END_CAM}..{END_ANT}")
feature("Board pocket", dict(rects=[(-18.85, -32.8, 18.85, 32.8)]), lambda: cut(False, 0, 4.0, 1, 0),
        "X +-18.85 Y -4..3.1 Z +-32.8")
feature("Antenna end lip", dict(rects=[(-18.85, LIP, 18.85, 32.8)]), lambda: boss(True, False, 3.1),
        f"Y 0..3.1 Z {LIP}..32.8")
feature("Antenna end slot", dict(rects=[(-18.4, LIP, 18.4, 34.8)]), lambda: cut(False, 0, 0.15, 0, 1.75),
        f"Y -0.15..1.75 Z {LIP}..34.8")
feature("USB-C windows", dict(rects=[(-21.0, -20.8, -18.5, -7.8), (18.5, -18.56, 21.0, -5.56)]),
        lambda: cut(False, 0, 5.13, 0, 1.87), "Y -5.13..1.87, USBC2 -X, USBC1 +X")
feature("J4 flex notch", dict(rects=[(-8.73, END_CAM - 0.2, 7.27, -32.5)]),
        lambda: cut(True, 1, 0, up=True, t0=3, start=1.6), "Y 1.6..3.1 camera end")
feature("Camera board pocket", dict(rects=[(-12.8, cz(-17.2), 12.8, cz(7.3))]), lambda: down(-LEDGE),
        f"X +-12.8 Y {LEDGE:.2f}")
feature("Holder pocket", dict(rects=[(-7.3, CAM_Z - 6.8, 7.3, CAM_Z + 6.8)]), lambda: down(-(LEDGE - FLANGE)),
        f"X +-7.3 Y {LEDGE - FLANGE:.2f}")
feature("Holder ear pocket", dict(rects=[(-11.3, CAM_Z - 2.3, 11.3, CAM_Z + 2.3)]), lambda: down(-(LEDGE - FLANGE)),
        f"X +-11.3 Y {LEDGE - FLANGE:.2f}")
feature("Camera FFC pocket", dict(rects=[(-9.1, cz(-17.9), 9.1, cz(-12.1))]), lambda: down(-(CAM_BACK - 4.39) + 0.3),
        f"Y {CAM_BACK - 4.69:.2f}")
feature("Flex channel and loop", dict(rects=[(-9.1, LOOP_END, 8.4, cz(-16.6))]), lambda: down(9.5),
        f"Y -9.5 Z {LOOP_END}..")
feature("Barrel hole", dict(circles=[(0, CAM_Z, 7.3)]), lambda: down(-BARREL_END), f"R 7.3 Y {BARREL_END:.2f}")
feature("Lens thread hole", dict(circles=[(0, CAM_Z, LENS_THREAD)]), lambda: below(BARREL_END),
        f"R {LENS_THREAD} Y {BOTTOM:.2f}..{BARREL_END:.2f}")
feature("Camera screw pilots", dict(circles=[(-x, cz(y), 0.8) for x, y in CAM_SCREWS]), lambda: down(-(LEDGE - 4.5)),
        f"4x Ø1.6 to Y {LEDGE - 4.5:.2f} (M2x5 thread-forming, button head)")
feature("Board support pad", dict(rects=[(8.55, -32.8, 16.25, -31.1)]), lambda: boss(True, True, 4.0),
        "X 8.55..16.25 Y -4..0 Z -32.8..-31.1")
feature("Board ribs", dict(rects=ribs), lambda: boss(True, True, 4.0), f"{len(ribs)} ribs Y -4..0")
feature("USB-C pads", dict(rects=USB_PADS), lambda: boss(True, True, 4.0), "Y -4..0")
feature("USB-C pads top", dict(rects=USB_PADS), lambda: down(3.25), "pads end at Y -3.25 (shell)")
feature("Board screw boss", dict(circles=[(*BOARD_HOLE, 2.0)]), lambda: boss(True, True, 4.0), "Ø4 Y -4..0")
feature("Board screw pilot", dict(circles=[(*BOARD_HOLE, 0.8)]), lambda: down(5.5),
        "Ø1.6 Y -5.5..0 (M2x6 thread-forming button head: an M2.5 head hits S2)")
sm.AddToDB = False

print("part box:", [round(v * 1000, 2) for v in model.GetPartBox(True)])
err, warn = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), err, warn), err.value)
