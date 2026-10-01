"""Build mechanical/ntc/MF55_NTC.SLDPRT: the cells' film thermistor (Shiheng MF55 103F3435: 10k 1%,
B25/85 3435, LCSC C394026), wired to JT (TS of the BQ24072). Run on the SolidWorks PC through the
solidworks-mcp run_python tool (it provides sw, get, nothing, byref_int).

Datasheet (approval sheet JK/JXJ190509017-A): polyimide film 5.0 max wide, 0.7 max thick, 25 +-1 long
overall; legs bare for 5 +-1, 0.5 x 0.15, 2.0 pitch; chip 2.5 from the tip.

Modelled as fitted, in base frame axes with the origin at JT's centre (X 17.0, Y 0, Z -5.34): legs
bent to JT's 1.6 pitch and soldered, the film up the lower cell's +X side, bent over it, into the
cells' 1.0 body gap (Y 12.14..13.14) with the upper cell glued on top.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\ntc\MF55_NTC.SLDPRT"
FILM_W, FILM_T, FILM_L = 5.0, 0.7, 20.0
LEG_W, LEG_T, LEG_PITCH = 0.5, 0.15, 1.6
FILM_Y0 = 5.6                                   # film starts: the legs' 5 less ~1 through the board
GAP_Y = 12.64                                   # middle of the cells' body gap
X_SIDE = -0.2                                   # film's inner face: 0.05 off the lower cell body (X 16.75)
UP = GAP_Y + FILM_T / 2 - FILM_Y0               # film running up the side
IN = FILM_L - UP                                # and in between the cells

model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension
sm.AddToDB = True


def top_box(name, rects, y0, y1):     # Top Plane: sketch (x, y) = model (X, -Z), extruded from Y y0 to y1
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for x0, z0, x1, z1 in rects:
        sm.CreateCornerRectangle(x0 * M, -z0 * M, 0, x1 * M, -z1 * M, 0)
    sm.InsertSketch(True)
    f = fm.FeatureExtrusion3(True, False, False, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                             False, False, False, False, True, True, True, 3 if y0 else 0, y0 * M, False)
    f.Name = name


top_box("Legs", [(-LEG_T / 2, z - LEG_W / 2, LEG_T / 2, z + LEG_W / 2) for z in (-LEG_PITCH / 2, LEG_PITCH / 2)],
        0.0, FILM_Y0 + 0.5)
top_box("Film up the cell side", [(X_SIDE, -FILM_W / 2, X_SIDE + FILM_T, FILM_W / 2)], FILM_Y0, GAP_Y + FILM_T / 2)
top_box("Film between the cells", [(X_SIDE + FILM_T - IN, -FILM_W / 2, X_SIDE, FILM_W / 2)],
        GAP_Y - FILM_T / 2, GAP_Y + FILM_T / 2)
sm.AddToDB = False
print("box", [round(v * 1000, 2) for v in model.GetPartBox(True)],
      f"expect X {X_SIDE + FILM_T - IN:.2f}..{X_SIDE + FILM_T} Y 0..{GAP_Y + FILM_T / 2:.2f} Z +-{FILM_W / 2}")
e, w = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), e, w), e.value)
sw.CloseDoc(get(model, "GetTitle"))
