"""Build mechanical/enclosure/cover.SLDPRT: the ABS top part over the board, the two cells and the
top camera. Run on the SolidWorks PC, after led_strips.py and clips.py (LED strip chamfers, slots and holes; seam lip and snap clips), through the solidworks-mcp run_python tool (it provides sw,
get, nothing, byref_int), like build_base.py, whose coordinates it shares:

  main board (MIFA.step x, y, z) -> X = x - 42.25, Y = -z, Z = y - 32.5; board back at Y 1.6
  cells (LiPo_503450) lie on the board back, 34.5 across, protection-board end at the board's
    camera-end edge (Z -32.5), far end Z 20.6 (board y 53.1, short of the antenna at y ~55)
  top camera (Arducam_B0370 x, y, z) -> X = x, Y = CAM2_BACK - z, Z = y + 1.1 + CAM_Z
    back to back with the bottom camera on the same axis, lens up through the top

The cover sits on the base's top face (Y 4.7, the probe wires' centre height). Assembly: cells in their bed; top camera into its
pocket with four M2 screws from its back into the cover; J4 flex from the board up the camera-end
riser, over the cells into the camera connector; cover onto the base, probe wires into their slots, its ribs on the board's antenna end.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\enclosure\cover.SLDPRT"

END_CAM, END_ANT = -39.5, 37.3                          # as the base
CAM_Z = (END_CAM + END_ANT) / 2
PARTING = 4.7                                           # base top face: the probe wires' centre height
OUTER_X, POCKET_X = 19.55, 18.45                        # as the base
BED = 6.6                                               # cells' lowest point: 0.2 over S1-S3 (4.8 on the board back)
CELL_BODY = BED + 0.54                                  # the cell body sits 0.54 above its protection-board end
CELL_GAP = 0.0                                          # the cells touch at their protection boards (6.0 thick there, the
                                                        # 5.0 bodies 1.0 apart: glue and the NTC film, 0.7)
# the film NTC (mechanical/ntc/MF55_NTC.SLDPRT) is soldered into JT through the battery pad slot, its leads
# up the lower cell's +X side, bent over it: the head lies in the cells' 1.5 glue gap; the cell wires pass outside
CELL_TOP = BED + 6.0 + CELL_GAP + 6.0                   # 18.6
PCM_END = -32.5 + 10.2                                  # protection board: Z -32.5 .. -22.3
HEADS = 1.9                                             # screw heads on the camera back over the cells: the holder screws (M2 pan, 1.6) + 0.3
CAM2_BACK = CELL_TOP + HEADS                            # 20.2
CAM_T, FLANGE, BARREL, CEIL = 2.38232, 3.0, 2.2, 1.82   # as the bottom camera and the base floor
LEDGE = CAM2_BACK + CAM_T                               # camera board lens-side face rests here
BARREL_END = LEDGE + FLANGE + BARREL
TOP = BARREL_END + CEIL                                 # 29.6
LENS_THREAD = 12.5 / 2
CAM_SCREWS = [(10.5, -7.52), (-10.5, -7.52), (10.5, 4.98), (-10.5, 4.98)]   # camera x, y of its Ø2.2 holes
FLEX_X = (-9.1, 8.4)                                    # J4 (x 33.8-49.3) and the camera FFC, as the base channel
FLEX_TOP = CAM2_BACK + 4.39 + 0.3                       # over the camera's FFC connector
MIC = (15.32, -29.94)                                   # U13 port hole (57.57, 2.56), opens on the board back
MIC_SLOT = 1.2                                          # a slot through the cradle from the port to its +X side,
MIC_TOP = 6.0                                           # out through a slot in the cover's +X wall, seam up to here
CRADLE_BOTTOM = PARTING + 0.05                          # the cell cradle (cradle.SLDPRT), on the board back
CRADLE_Z0, CRADLE_Z1 = -33.55, 21.65                    # its ends: 1 mm borders beyond the cell groove
CRADLE_X = POCKET_X - 0.05
SOLDER_SLOT = (15.9, -8.4, CRADLE_X + 0.1, 2.9)         # over JB1/JB2/JT (board x 59.25, y 26.4-34.4): cell wires and the
                                                        # NTC film (5.0 max wide, centred on JT at Z -5.34) go up
RAILS = [(17.3, CRADLE_X), (-CRADLE_X, -17.3)]          # alignment rails into the base pocket at the camera end
RAIL_Z, RAIL_BOTTOM = (-32.75, -28.75), 2.7
BOARD_HOLE = (-15.64, -6.64)                            # main board Ø2.6 hole: the board screw holds the cradle
# room for the seam's snap clips to bend in, 1.2 deep into the cradle; merged into the neighbouring
# cuts where less than 0.6 mm would be left (the mic slot, the J4 cut)
CLIP_ROOM = [(CRADLE_X - 1.2, MIC[1] - MIC_SLOT / 2, CRADLE_X + 0.1, TAB_Z[0] + TAB_W_SIDE / 2 + 0.5),   # +X: into the mic slot
             (-CRADLE_X - 0.1, TAB_Z[0] - TAB_W_SIDE / 2 - 0.5, -CRADLE_X + 1.2, TAB_Z[0] + TAB_W_SIDE / 2 + 0.5)] + \
            [(x0, TAB_Z[1] - TAB_W_SIDE / 2 - 0.5, x1, TAB_Z[1] + TAB_W_SIDE / 2 + 0.5)
             for x0, x1 in ((CRADLE_X - 1.2, CRADLE_X + 0.1), (-CRADLE_X - 0.1, -CRADLE_X + 1.2))] + \
            [(TAB_X[-1][1] - TAB_W / 2 - 0.5, CRADLE_Z0 - 0.1, TAB_X[-1][1] + TAB_W / 2 + 0.5, CRADLE_Z0 + 1.0),
             (TAB_X[-1][0] - TAB_W / 2 - 0.5, CRADLE_Z0 - 0.1, FLEX_X[0] + 0.1, CRADLE_Z0 + 1.0)]   # -X: into the J4 cut


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


S_POCKETS = [(-POCKET_X, -4.75, -9.2, 1.75),       # S2 (x 25.2-32.8, y 28.05-33.95) with its plug to X -18.2
             (9.4, -23.82, POCKET_X, -17.32),      # S1 (x 51.87-59.47, y 8.98-14.88), plug to X 18.37
             (9.2, 14.25, POCKET_X, CRADLE_Z1 + 0.1)]   # S3 (x 51.7-59.3, y 47.05-52.95), out through the cradle's end

sm.AddToDB = True
feature("Block", dict(rects=[(-OUTER_X, END_CAM, OUTER_X, END_ANT)]), lambda: block(PARTING, TOP),
        f"X +-{OUTER_X} Y {PARTING}..{TOP:.2f} Z {END_CAM}..{END_ANT}")
feature("Cell pocket", dict(rects=[(-POCKET_X, CRADLE_Z0 - 0.05, POCKET_X, CRADLE_Z1 + 0.05)]), lambda: span(PARTING, CELL_TOP + 0.2),
        f"open to the seam: the cells, on their cradle on the board, go in from below; X +-{POCKET_X} for the LED strips")
feature("LED clamp ear ways", dict(rects=[(12.7, 20.7, 18.0, STRIP_Z1 + 0.1 + EAR_L), (-18.0, 20.7, -12.7, STRIP_Z1 + 0.1 + EAR_L)]),
        lambda: span(PARTING, CELL_TOP + 0.2), "the clamps' antenna-end ears go up beside the cell pocket's end")
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
feature("Camera FFC pocket", dict(rects=[(-9.1, cz(-17.9), 9.1, cz(-12.1))]), lambda: span(CELL_TOP + 0.2, FLEX_TOP),
        f"Y ..{FLEX_TOP:.2f}")
feature("Barrel hole", dict(circles=[(0, CAM_Z, 7.3)]), lambda: span(CELL_TOP + 0.2, BARREL_END),
        f"R 7.3 Y ..{BARREL_END:.2f}")
feature("Lens thread hole", dict(circles=[(0, CAM_Z, LENS_THREAD)]), lambda: span(BARREL_END - 0.1),
        f"R {LENS_THREAD} Y {BARREL_END:.2f}..{TOP:.2f}")
feature("Camera screw pilots", dict(circles=[(x, cz(y), 0.8) for x, y in CAM_SCREWS]), lambda: span(LEDGE - 0.1, LEDGE + 4.5),
        f"4x Ø1.6 Y {LEDGE:.2f}..{LEDGE + 4.5:.2f} (M2x5 thread-forming, button head)")
feature("Board hold-down ribs", dict(rects=[(x - 1.0, 27.5, x + 1.0, 29.5) for x in (-12.0, 0.0, 12.0)]),
        lambda: block(1.65, PARTING + 0.1), "press on the board back at its antenna end (no parts there)")
feature("Mic slot", dict(rects=[(POCKET_X - 0.1, MIC[1] - MIC_SLOT / 2, OUTER_X + 0.2, MIC[1] + MIC_SLOT / 2)]),
        lambda: span(PARTING, MIC_TOP), f"the cradle's mic slot out through the +X wall, Y {PARTING}..{MIC_TOP}, open to the seam")
# cells: in the cradle's groove below, ribs at the ends and (outside the LED strips' way down) the
# camera-end sides, the LED clamps on top; glued to the cradle and to each other, the NTC's film between them.
# The ribs start over the cradle's face
CELL_X, CELL_Z0, CELL_Z1 = 17.25, -32.5, 20.6
BODY_TOP = CELL_TOP - 0.46                              # the upper cell's body top: its protection board stands 0.46 over it
RIM = BED + 0.05                                        # just over the cradle's flat face
feature("Cell side rib", dict(rects=[(-POCKET_X, -28.75, -CELL_X, -27.25)]),
        lambda: block(RIM, CELL_TOP + 0.2), f"camera end, X -{POCKET_X}..-{CELL_X} (further along, the LED strips' way in)")
feature("Cell side rib, +X", dict(rects=[(CELL_X, -28.75, POCKET_X, -27.25)]),
        lambda: block(CELL_TOP - 6.0 + 0.4, CELL_TOP + 0.2), "the upper cell only: the cell wires run along the lower one, under it")
feature("Cell end ribs, far end", dict(rects=[(x - 0.75, CELL_Z1, x + 0.75, CRADLE_Z1 + 0.05) for x in (-10.0, 0.0, 10.0)]),
        lambda: block(RIM, CELL_TOP + 0.2), "3")
feature("Cell end ribs, camera end", dict(rects=[(x - 0.75, CRADLE_Z0 - 0.05, x + 0.75, CELL_Z0) for x in (-14.0,)]),
        lambda: block(RIM, CELL_TOP + 0.2), "-X of the J4 riser: +X of it the cell wires run along the end face")
TOP_PADS = [(-10.0, 11.0), (0.0, 11.0), (10.0, 11.0), (-10.0, 18.0), (0.0, 18.0), (10.0, 18.0),   # beyond the camera pocket
            (-10.6, -20.5), (10.6, -20.5)]                                                    # beside the flex channel
feature("Cell top pads", dict(rects=[(x - 1.0, z - 1.0, x + 1.0, z + 1.0) for x, z in TOP_PADS]),
        lambda: block(BODY_TOP, CELL_TOP + 0.2), f"{len(TOP_PADS)} pads down to Y {BODY_TOP:.2f}: on the upper cell's body, it cannot move")
seam_cover_features(POCKET_X, *SEAM_Z, side_gaps=plug_gaps(POCKET_X),
                    end_gaps={-1: [(FLEX_X[0] - 0.2, FLEX_X[1] + 0.2)]})   # the probe plugs; the J4 flex up its riser
seam_screw_cover_features()
plug_features("cover", OUTER_X)
led_strip_features(1, TOP, OUTER_X, END_CAM, END_ANT, CELL_TOP + 0.2)
sm.AddToDB = False

print("part box:", [round(v * 1000, 2) for v in model.GetPartBox(True)])
err, warn = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), err, warn), err.value)

# the strip clamps: one part for both sides (built for +X; the -X one is it turned 180 degrees about
# the vertical through the strip's middle), its face 0.2 past the cell pocket's ceiling onto the cell
CLAMPS = r"C:\hw\LEAKCAM\mechanical\enclosure"
build_clamp(CLAMPS + r"\clamp_cover.SLDPRT", 1, 1, TOP, OUTER_X, CELL_TOP + 0.2, 0.2,
            wire_z=[(WIRE_Z[0], STRIP_Z0 + STRIP_Z1 - WIRE_Z[0])])   # the -X copy turned: its pads at the mirrored Z


# the cell cradle: a tray on the board back under the cells, fitted before them (they are soldered to
# the board and glued to it and to each other, then the cover goes over). The lower cell sits in its
# 0.5 mm groove; the tray has room for S1-S3 and their plugs, J4 and its flex, an open slot over the
# battery and NTC pads (cell wires and the NTC film up beside the cells, soldered with the cradle on) and
# a slot from the mic port out to its +X side, and room at the seam's snap clips to bend in. Held by the board screw (M2x10 through its foot, the board and the base clamp's boss), located
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
        ("Board screw foot", dict(circles=[(*BOARD_HOLE, 2.0)]), (1.6, CRADLE_BOTTOM + 0.01), True),
        ("Board screw hole", dict(circles=[(*BOARD_HOLE, 1.1)]), (1.5, rim + 0.1), False),
        ("Board screw counterbore", dict(circles=[(*BOARD_HOLE, 1.95)]), (CELL_BODY - 1.2, rim + 0.1), False),
        ("Mic slot", dict(rects=[(MIC[0] - MIC_SLOT / 2, MIC[1] - MIC_SLOT / 2, CRADLE_X + 0.1, MIC[1] + MIC_SLOT / 2)]),
         (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
        ("Snap clip room", dict(rects=CLIP_ROOM), (CRADLE_BOTTOM - 0.1, rim + 0.1), False),
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


build_cradle(CLAMPS + r"\cradle.SLDPRT")
