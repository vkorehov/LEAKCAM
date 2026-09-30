"""Build mechanical/enclosure/cover.SLDPRT: the ABS top part over the board, the two cells and the
top camera. Run on the SolidWorks PC through the solidworks-mcp run_python tool (it provides sw,
get, nothing, byref_int), like build_base.py, whose coordinates it shares:

  main board (MIFA.step x, y, z) -> X = x - 42.25, Y = -z, Z = y - 32.5; board back at Y 1.6
  cells (LiPo_503450) lie on the board back, 34.5 across, protection-board end at the board's
    camera-end edge (Z -32.5), far end Z 20.6 (board y 53.1, short of the antenna at y ~55)
  top camera (Arducam_B0370 x, y, z) -> X = x, Y = CAM2_BACK - z, Z = y + 1.1 + CAM_Z
    back to back with the bottom camera on the same axis, lens up through the top

The cover sits on the base's top face (Y 4.7, the probe wires' centre height). Assembly: cells in their bed; top camera into its
pocket with four M2 screws from its back into the cover; J4 flex from the board up the camera-end
riser, over the cells into the camera connector; cover onto the base, probe wires into their slots.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\enclosure\cover.SLDPRT"

END_CAM, END_ANT = -39.5, 37.3                          # as the base
CAM_Z = (END_CAM + END_ANT) / 2
PARTING = 4.7                                           # base top face: the probe wires' centre height
OUTER_X, POCKET_X = 19.55, 18.45                        # as the base
BED = 6.6                                               # cells' lowest point: 0.2 over S1-S3 (4.8 on the board back)
CELL_BODY = BED + 0.54                                  # the cell body sits 0.54 above its protection-board end
CELL_TOP = BED + 6.0 + 0.2 + 6.0                        # two cells, 0.2 apart: 18.8
PCM_END = -32.5 + 10.2                                  # protection board: Z -32.5 .. -22.3
HEADS = 1.4                                             # M2 button heads (1.1) between cells and camera
CAM2_BACK = CELL_TOP + HEADS                            # 20.2
CAM_T, FLANGE, BARREL, CEIL = 2.38232, 3.0, 2.2, 1.82   # as the bottom camera and the base floor
LEDGE = CAM2_BACK + CAM_T                               # camera board lens-side face rests here
BARREL_END = LEDGE + FLANGE + BARREL
TOP = BARREL_END + CEIL                                 # 29.6
LENS_THREAD = 12.5 / 2
CAM_SCREWS = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]   # camera x, y of its Ø2.2 holes
FLEX_X = (-9.1, 8.4)                                    # J4 (x 33.8-49.3) and the camera FFC, as the base channel
FLEX_TOP = CAM2_BACK + 4.39 + 0.3                       # over the camera's FFC connector
WIRES = [(-1, -2.5), (-1, -0.5), (1, -21.57), (1, -19.57), (1, 16.5), (1, 18.5)]   # probe wires: wall side, Z
WIRE_TOP = PARTING + 0.55                               # wires at Y 4.7, Ø1.0: the upper half of each hole
MIC = (15.32, -29.94)                                   # U13 port hole (57.57, 2.56), opens on the board back
MIC_TUBE = 1.8                                          # tube end: 0.2 over the board back (gasket)
MIC_DUCT = (5.0, 6.0)                                   # closed duct inside the cover floor, to the +X wall


def cz(y):                # camera y -> base Z
    return y + 1.1 + CAM_Z


for d in get(sw, "GetDocuments") or ():
    if get(d, "GetTitle").lower().startswith("cover"):
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


def block(y0, y1):                  # boss from Y y0 up to y1 (start offset along the normal)
    return fm.FeatureExtrusion3(True, False, False, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                                False, False, False, False, True, True, True, 3, y0 * M, False)


def tube(y0, y1):                   # boss from Y y0 down to y1 (start offset along the normal, reversed)
    return fm.FeatureExtrusion3(True, False, True, 0, 0, (y0 - y1) * M, 0, False, False, False, False, 0, 0,
                                False, False, False, False, True, True, True, 3, y0 * M, False)


def span(y0, y1=None):              # cut from Y y0 up to y1, or up through the top
    t1, d = (1, 0) if y1 is None else (0, y1 - y0)
    return fm.FeatureCut4(True, False, True, t1, 0, d * M, 0, False, False, False, False, 0, 0,
                          False, False, False, False, False, True, True, False, False, False,
                          3, y0 * M, False, False)


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


S_POCKETS = [(-POCKET_X, -4.75, -9.2, 1.75),       # S2 (x 25.2-32.8, y 28.05-33.95) with its plug to X -18.2
             (9.4, -23.82, POCKET_X, -17.32),      # S1 (x 51.87-59.47, y 8.98-14.88), plug to X 18.37
             (9.2, 14.25, POCKET_X, 20.75)]        # S3 (x 51.7-59.3, y 47.05-52.95)

sm.AddToDB = True
feature("Block", dict(rects=[(-OUTER_X, END_CAM, OUTER_X, END_ANT)]), lambda: block(PARTING, TOP),
        f"X +-{OUTER_X} Y {PARTING}..{TOP:.2f} Z {END_CAM}..{END_ANT}")
feature("Cell pocket", dict(rects=[(-17.45, -32.7, 17.45, 20.8)]), lambda: span(CELL_BODY, CELL_TOP + 0.2),
        f"X +-17.45 Y {CELL_BODY:.2f}..{CELL_TOP + 0.2:.2f} Z -32.7..20.8")
feature("Cell protection-board end", dict(rects=[(-17.45, -32.7, 17.45, PCM_END + 0.2)]), lambda: span(BED, CELL_BODY),
        f"Y {BED}..{CELL_BODY:.2f} Z -32.7..{PCM_END + 0.2:.1f}")
feature("S1-S3 and plug pockets", dict(rects=S_POCKETS), lambda: span(PARTING, BED), f"Y {PARTING}..{BED}")
feature("J4 and flex entry", dict(rects=[(FLEX_X[0], -37.5, FLEX_X[1], -26.8)]), lambda: span(PARTING, BED),
        f"Y {PARTING}..{BED} Z -37.5..-26.8")
feature("J4 flex riser", dict(rects=[(FLEX_X[0], -37.5, FLEX_X[1], -32.7)]), lambda: span(PARTING, FLEX_TOP),
        f"Y {PARTING}..{FLEX_TOP:.2f} Z -37.5..-32.7")
feature("Flex channel over the cells", dict(rects=[(FLEX_X[0], -37.5, FLEX_X[1], cz(-16.6))]),
        lambda: span(CELL_TOP + 0.2, FLEX_TOP), f"Y {CELL_TOP + 0.2:.1f}..{FLEX_TOP:.2f}")
feature("Camera board pocket", dict(rects=[(-12.8, cz(-17.2), 12.8, cz(7.3))]), lambda: span(CELL_TOP + 0.2, LEDGE),
        f"X +-12.8 Y ..{LEDGE:.2f}")
feature("Holder pocket", dict(rects=[(-7.3, CAM_Z - 6.8, 7.3, CAM_Z + 6.8)]), lambda: span(LEDGE - 0.1, LEDGE + FLANGE),
        f"Y ..{LEDGE + FLANGE:.2f}")
feature("Holder ear pocket", dict(rects=[(-11.3, CAM_Z - 2.3, 11.3, CAM_Z + 2.3)]), lambda: span(LEDGE - 0.1, LEDGE + FLANGE),
        f"Y ..{LEDGE + FLANGE:.2f}")
feature("Camera FFC pocket", dict(rects=[(-9.1, cz(-17.9), 9.1, cz(-12.1))]), lambda: span(LEDGE - 0.1, FLEX_TOP),
        f"Y ..{FLEX_TOP:.2f}")
feature("Barrel hole", dict(circles=[(0, CAM_Z, 7.3)]), lambda: span(LEDGE + FLANGE - 0.1, BARREL_END),
        f"R 7.3 Y ..{BARREL_END:.2f}")
feature("Lens thread hole", dict(circles=[(0, CAM_Z, LENS_THREAD)]), lambda: span(BARREL_END - 0.1),
        f"R {LENS_THREAD} Y {BARREL_END:.2f}..{TOP:.2f}")
feature("Camera screw pilots", dict(circles=[(x, cz(y), 0.8) for x, y in CAM_SCREWS]), lambda: span(LEDGE - 0.1, LEDGE + 4.5),
        f"4x Ø1.6 Y {LEDGE:.2f}..{LEDGE + 4.5:.2f} (M2x5 thread-forming, button head)")
feature("Probe wire half-slots", dict(rects=[((POCKET_X - 0.3 if s > 0 else -OUTER_X - 0.2), z - 0.55, (OUTER_X + 0.2 if s > 0 else -POCKET_X + 0.3), z + 0.55)
                                             for s, z in WIRES]), lambda: span(PARTING, WIRE_TOP),
        f"6 slots Y {PARTING}..{WIRE_TOP:.2f}: the upper half of each wire hole")
feature("Mic tube", dict(circles=[(*MIC, 1.5)]), lambda: tube(PARTING, MIC_TUBE), f"Ø3 Y {MIC_TUBE}..{PARTING} over the port")
feature("Mic bore", dict(circles=[(*MIC, 0.5)]), lambda: span(MIC_TUBE - 0.1, MIC_DUCT[1]), f"Ø1 Y {MIC_TUBE}..{MIC_DUCT[1]}")
feature("Mic duct", dict(rects=[(MIC[0] - 0.5, MIC[1] - 0.5, OUTER_X + 0.2, MIC[1] + 0.5)]), lambda: span(*MIC_DUCT),
        f"1 x 1 closed duct to the +X wall, Y {MIC_DUCT[0]}..{MIC_DUCT[1]}")
sm.AddToDB = False

print("part box:", [round(v * 1000, 2) for v in model.GetPartBox(True)])
err, warn = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), err, warn), err.value)
