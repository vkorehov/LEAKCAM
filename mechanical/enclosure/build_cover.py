"""Build mechanical/enclosure/cover.SLDPRT: the ABS top part over the board, the two cells and the
top camera. Run on the SolidWorks PC, after clips.py (seam lip, seam screws), through the solidworks-mcp run_python tool (it provides sw,
get, nothing, byref_int), like build_base.py, whose coordinates it shares:

  main board (MIFA.step x, y, z) -> X = x - 42.25, Y = -z, Z = y - 32.5; board back at Y 1.6
  cells (LiPo_503450, measured 53.0 x 35.0 x 5.3 with protection board and wires) lie on the board
    back, protection-board end at the board's camera-end edge (Z -32.5), far end Z 20.5 (board y 53.0,
    short of the antenna at y ~55)
  top camera (Arducam_B0370 x, y, z) -> X = x, Y = CAM2_BACK - z, Z = y + 1.1 + CAM_Z
    back to back with the bottom camera on the same axis, lens up through the top

The cover sits on the base's top face (Y 4.7, the probe wires' centre height). Assembly: cells in their bed; top camera into its
pocket with four M2 screws from its back into the cover; J4 flex from the board up the camera-end
riser, over the cells into the camera connector; probe wires laid into their channels and tubes glued in; cover onto the base.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\enclosure\cover.SLDPRT"

END_CAM, END_ANT = -39.5, 37.3                          # as the base
CAM_Z = (END_CAM + END_ANT) / 2
PARTING = 4.7                                           # base top face: the probe wires' centre height
OUTER_X, POCKET_X = 19.55, 18.45                        # as the base
BED = 6.6                                               # cells' lowest point: 0.2 over S1-S3 (4.8 on the board back)
CELL_T, CELL_W, CELL_L = 5.3, 35.0, 53.0                # measured overall, protection board and wire ends included
CELL_GAP = 0.7                                          # between the cells: the NTC film (0.7) and glue
# the film NTC (mechanical/ntc/MF55_NTC.SLDPRT) is soldered into JT through the battery pad slot, its leads
# up the lower cell's +X side, bent over it: the head lies in the cells' gap; the cell wires pass outside
CELL_TOP = BED + CELL_T + CELL_GAP + CELL_T             # 17.9
PCM_END = -32.5 + 10.2                                  # protection board: Z -32.5 .. -22.3
HEADS = 1.9                                             # screw heads on the camera back over the cells: the holder screws (M2 pan, 1.6) + 0.3
CAM2_BACK = CELL_TOP + HEADS                            # 19.8: right over the cells (holder screw heads 0.3 off); the form's
                                                        # crater is round the lens there (lens_raise_mm 0); estimated lens
CAM_T, FLANGE, BARREL, CEIL = 2.38232, 3.0, 2.2, 1.82   # as the bottom camera and the base floor
LEDGE = CAM2_BACK + CAM_T                               # camera board lens-side face rests here
BARREL_END = LEDGE + FLANGE + BARREL
TOP = BARREL_END + CEIL                                 # 33.9: the old top over the barrel
LENS_THREAD = 12.5 / 2
LENS_BORE = 8.1                                         # past the old top: round the lens body (Ø15), up to the crater
CAM_PILOT = 3.5                                         # camera screw pilot depth: M2x5 through the 2.4 board leaves 2.6 of
                                                        # thread, 0.9 spare; >= 1 mm of plastic past the tip
CAM_SCREWS = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]   # camera x, y of its Ø2.2 holes
FLEX_X = (-9.1, 8.4)                                    # J4 (x 33.8-49.3) and the camera FFC, as the base channel
FLEX_TOP = CAM2_BACK + 4.39 + 0.3                       # over the camera's FFC connector
RISER_TOP, FLEX_RAMP = 21.19, (-30.0, -17.9)            # the J4 riser stays low (the form is low at the end: 3.5 over the
                                                        # cells), then the ramp up to the camera's connector, Z
MIC = (15.32, -29.94)                                   # U13 port hole (57.57, 2.56), opens on the board back
MIC_SLOT = 1.2                                          # a slot through the cradle from the port to its +X side,
MIC_TOP = 6.0                                           # out through a slot in the cover's +X wall, seam up to here
CRADLE_BOTTOM = PARTING + 0.2                           # the cell cradle (cradle.SLDPRT), 0.2 over the base's seam face
CELL_POCKET_Z = (-33.6, 21.7)                           # the cover's cell pocket ends (the printed cover: unchanged)
CRADLE_Z0, CRADLE_Z1 = CELL_POCKET_Z[0] + 0.2, CELL_POCKET_Z[1] - 0.2   # its ends: 0.2 inside the cover's pocket
CRADLE_X = POCKET_X - 0.2                               # 0.2 off the pockets' side walls (base and cover)
SOLDER_SLOT = (15.9, -8.4, CRADLE_X + 0.1, 2.9)         # over JB1/JB2/JT (board x 59.25, y 26.4-34.4): cell wires and the
                                                        # NTC film (5.0 max wide, centred on JT at Z -5.34) go up
RAILS = [(16.4, CRADLE_X), (-CRADLE_X, -16.4)]          # alignment rails into the base pocket at the camera end (2 mm)
RAIL_Z, RAIL_BOTTOM = (-32.6, -28.75), 2.7                # 0.2 off the base pocket's end (Z -32.8)
BOARD_HOLE = (-15.64, -6.64)                            # main board Ø2.6 hole: the board screw holds the cradle
SCREW_HEAD_FLOOR = BED - 1.25                           # its button head (1.1) 0.15 under the flat cell, 0.6 cradle under it


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


TONGUES = [(CRADLE_X - 0.01, 2.9, 23.39, 5.1), (CRADLE_X - 0.01, 9.9, 23.39, 14.25)]   # the battery plate's +X tongues: the
                                                        # cover drops over them (its slots, form_params user_cuts)
FOOT_X0 = -17.75          # the board screw foot's -X edge: 1.0 round the Ø2.2 hole (S2's wire run passes over the tray)
S_POCKETS = [(-POCKET_X, -4.75, -9.2, 1.75),       # S2 (x 25.2-32.8, y 28.05-33.95) with its plug to X -18.2
             (9.4, -23.82, POCKET_X, -17.32),      # S1 (x 51.87-59.47, y 8.98-14.88), plug to X 18.37
             (9.2, 14.25, POCKET_X, CRADLE_Z1 + 0.1)]   # S3 (x 51.7-59.3, y 47.05-52.95), out through the cradle's end

sm.AddToDB = True
form_start("cover")
feature("Cell pocket", dict(rects=[(-POCKET_X, CELL_POCKET_Z[0], POCKET_X, CELL_POCKET_Z[1])]), lambda: span(PARTING, CELL_TOP + 0.2),
        f"open to the seam: the cells, on their cradle on the board, go in from below")
feature("J4 flex riser", dict(rects=[(FLEX_X[0], -37.5, FLEX_X[1], -32.7)]), lambda: span(PARTING, RISER_TOP),
        f"Y {PARTING}..{RISER_TOP:.2f} Z -37.5..-32.7")
feature("Flex channel over the cells", dict(rects=[(FLEX_X[0], -37.5, FLEX_X[1], FLEX_RAMP[0])]),
        lambda: span(CELL_TOP + 0.2, RISER_TOP), f"Y {CELL_TOP + 0.2:.1f}..{RISER_TOP:.2f}")
rp_feature("Flex channel ramp", [(FLEX_RAMP[0], CELL_TOP + 0.2), (FLEX_RAMP[0], RISER_TOP), (FLEX_RAMP[1], FLEX_TOP),
                                 (cz(-16.6), FLEX_TOP), (cz(-16.6), CELL_TOP + 0.2)], FLEX_X[0], FLEX_X[1], False,
           f"up to the camera's connector, Y {FLEX_TOP:.2f} at Z {FLEX_RAMP[1]}")
feature("Camera board pocket", dict(rects=[(-12.8, cz(-17.2), 12.8, cz(7.3))]), lambda: span(CELL_TOP + 0.2, LEDGE),
        f"X +-12.8 Y ..{LEDGE:.2f}")
feature("Holder pocket", dict(rects=[(-7.3, CAM_Z - 6.8, 7.3, CAM_Z + 6.8)]), lambda: span(LEDGE - 0.1, LEDGE + FLANGE),
        f"Y ..{LEDGE + FLANGE:.2f}")
feature("Holder ear pocket", dict(rects=[(-11.3, CAM_Z - 2.3, 11.3, CAM_Z + 2.3)]), lambda: span(LEDGE - 0.1, LEDGE + FLANGE),
        f"Y ..{LEDGE + FLANGE:.2f}")
feature("Camera FFC pocket", dict(rects=[(-9.1, cz(-17.9), 9.1, cz(-12.1))]), lambda: span(CELL_TOP + 0.2, FLEX_TOP),
        f"Y ..{FLEX_TOP:.2f}")
feature("Barrel hole", dict(circles=[(0, CAM_Z, 7.3)]), lambda: span(CELL_TOP + 0.2, BARREL_END),
        f"R 7.3 Y ..{BARREL_END:.2f}")
feature("Lens thread hole", dict(circles=[(0, CAM_Z, LENS_THREAD)]), lambda: span(BARREL_END - 0.1),
        f"R {LENS_THREAD} Y {BARREL_END:.2f}..")
feature("Lens bore", dict(circles=[(0, CAM_Z, LENS_BORE)]), lambda: span(TOP), f"R {LENS_BORE} from Y {TOP:.2f} out: the lens body")
feature("Camera screw pilots", dict(circles=[(x, cz(y), 0.8) for x, y in CAM_SCREWS]), lambda: span(LEDGE - 0.1, LEDGE + CAM_PILOT),
        f"4x Ø1.6 Y {LEDGE:.2f}..{LEDGE + CAM_PILOT:.2f} (M2x5 thread-forming, button head)")
feature("Mic slot", dict(rects=[(POCKET_X - 0.1, MIC[1] - MIC_SLOT / 2, SKIN_OUT, MIC[1] + MIC_SLOT / 2)]),
        lambda: span(PARTING, MIC_TOP), f"the cradle's mic slot out through the +X wall, Y {PARTING}..{MIC_TOP}, open to the seam")
# the cover holds neither the cells nor the board (the cells are glued to the cradle and to each other,
# the NTC's film between them; the board screw holds board and cradle): no ribs or pads on them
seam_screw_cover_features()
seam_screw_base_features(-SKIN_OUT, optional=True)   # where the cover's end caps come down past the seam, the screws
                                                    # pass them (none where the seam is flat at the screws)
plug_features("cover", OUTER_X)
form_trim("cover")
# the lip hangs below the seam, outside the cover's piece of the form: after the trim
seam_cover_features(POCKET_X, *SEAM_Z, side_gaps=plug_gaps(POCKET_X),
                    end_gaps={-1: [(FLEX_X[0] - 0.2, FLEX_X[1] + 0.2)]})   # the probe plugs; the J4 flex up its riser
legs_cut("cover")
leds_cut("cover")
shell_cut("cover")
sm.AddToDB = False

print("part box:", [round(v * 1000, 2) for v in model.GetPartBox(True)])
err, warn = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), err, warn), err.value)

PARTS = r"C:\hw\LEAKCAM\mechanical\enclosure"


# the cell cradle: a tray on the board back under the cells, fitted before them (they are soldered to
# the board and glued to it and to each other, then the cover goes over). The lower cell lies on its
# flat face; the tray has room for S1-S3 and their plugs, J4 and its flex, an open slot over the
# battery and NTC pads (cell wires and the NTC film up beside the cells, soldered with the cradle on) and
# a slot from the mic port out to its +X side. Held by the board screw (M2x8 through its foot and the board into the base's boss), located
# by two rails at the camera end that drop inside the base pocket's walls.
def build_cradle(path):
    global model, sm, fm, ext
    keep = model, sm, fm, ext
    model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
    sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension
    sm.AddToDB = True
    rim = BED                       # one flat face under the cells: printed face down, no supports
    parts = [
        ("Tray", dict(rects=[(-CRADLE_X, CRADLE_Z0, CRADLE_X, CRADLE_Z1)]), (CRADLE_BOTTOM, rim), True),
        ("S1-S3 and plugs", dict(rects=S_POCKETS), (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
        ("J4 and its flex", dict(rects=[(FLEX_X[0], CRADLE_Z0 - 0.1, FLEX_X[1], -26.8)]), (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
        ("Battery and NTC pad slot", dict(rects=[SOLDER_SLOT]), (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
        ("Alignment rails", dict(rects=[(x0, RAIL_Z[0], x1, RAIL_Z[1]) for x0, x1 in RAILS]), (RAIL_BOTTOM, CRADLE_BOTTOM + 0.01), True),
        ("Board screw foot", dict(rects=[(FOOT_X0, BOARD_HOLE[1] - 2.1, BOARD_HOLE[0] + 2.1, BOARD_HOLE[1] + 2.1)]),
         (1.6, CRADLE_BOTTOM + 0.01), True),                                     # square, under the tray
        ("Board screw hole", dict(circles=[(*BOARD_HOLE, 1.1)]), (1.5, rim + 0.1), False),
        ("Board screw counterbore", dict(circles=[(*BOARD_HOLE, 1.95)]), (SCREW_HEAD_FLOOR, rim + 0.1), False),
        ("Mic slot", dict(rects=[(MIC[0] - MIC_SLOT / 2, MIC[1] - MIC_SLOT / 2, CRADLE_X + 0.1, MIC[1] + MIC_SLOT / 2)]),
         (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
        ("Web by the S2 pocket", dict(rects=[(BOARD_HOLE[0] - 1.95, BOARD_HOLE[1], BOARD_HOLE[0] + 1.95, -4.75)]),
         (SCREW_HEAD_FLOOR, rim + 0.1), False),   # the counterbore widened to the S2 pocket: tangent, its depth only
        ("-X edge for the S2 wires", dict(rects=[(-CRADLE_X - 0.1, 1.75, -17.2, CRADLE_Z1 + 0.1)]), (CRADLE_BOTTOM - 0.1, rim + 0.1), False),   # fit Sketch3 (10-10)
        ("+X tongues", dict(rects=TONGUES), (CRADLE_BOTTOM, rim), True),   # fit Sketch4 (10-10): into the cover's slots opposite the screw
    ]
    for name, geo, (ya, yb), boss in parts:
        f = top_span(ya=ya, yb=yb, boss=boss, **geo)
        if f is None:
            raise RuntimeError(f"cradle {name}: feature failed")
        f.Name = name
        print(f"  cradle {name:28s} {fbox(f)}")
    sm.AddToDB = False
    print("cradle box", [round(q * 1000, 2) for q in model.GetPartBox(True)])
    e, w = byref_int(), byref_int()
    print("saved:", ext.SaveAs(path, 0, 1, nothing(), e, w), e.value)
    sw.CloseDoc(get(model, "GetTitle"))
    model, sm, fm, ext = keep


build_cradle(PARTS + r"\cradle.SLDPRT")
