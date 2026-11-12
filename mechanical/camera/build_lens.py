"""Build mechanical/camera/M12_fisheye_222.SLDPRT: the cameras' 222 degree M12 fisheye (EXTERNAL_PARTS.md,
part 4), screwed in from outside after assembly through the enclosure's lens thread hole. Run on the
SolidWorks PC through the solidworks-mcp run_python tool (it provides sw, get, nothing, byref_int).

Measured by the user 2026-11-11 (README.md "The 222 degree camera, measured"): the thread 11.8 mm across, 8.4 long;
the front 16 mm across, a 3 mm cylinder, then the glass as an elliptic dome 2 mm over it.

Part frame: axis +Y outwards, origin on the holder's top face (where the enclosure's barrel pocket
ends); the thread runs THREAD_IN into the holder and through the CEIL wall, the body stands on the
enclosure surface.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\camera\M12_fisheye_222.SLDPRT"
THREAD_D, THREAD_L = 11.8, 8.4                  # M12 x 0.5 as measured (11.8 over the thread), its whole length
CEIL = 1.82                                     # enclosure wall over the holder (build_base.py, build_cover.py)
THREAD_IN = THREAD_L - CEIL                     # the thread's part inside the holder, the rest in the wall
BODY_D, BODY_L = 16.0, 3.0                      # the front's cylinder
GLASS_H = 2.0                                   # the glass: an elliptic dome over the whole 16 mm, this high

model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension
sm.AddToDB = True


def cylinder(name, d, y0, y1):          # Top Plane (normal +Y): circle on the axis, from Y y0 to y1
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    sm.CreateCircleByRadius(0, 0, 0, d / 2 * M)
    sm.InsertSketch(True)
    f = fm.FeatureExtrusion3(True, False, False, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                             False, False, False, False, True, True, True, 3, abs(y0) * M, y0 < 0)
    f.Name = name


def dome(name, d, y0, h, n=12):
    """a quarter ellipse (d / 2 by h) revolved about the axis, from Y y0: the glass; a polyline through points on the
    ellipse (all inside it)"""
    import math
    model.ClearSelection2(True)
    assert ext.SelectByID2("Front Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    pts = [(d / 2 * math.cos(math.pi / 2 * i / n), y0 + h * math.sin(math.pi / 2 * i / n)) for i in range(n + 1)]
    pts = [(0, y0)] + pts                       # from the axis out, up the ellipse, back on the axis
    for (x0, y_0), (x1, y_1) in zip(pts, pts[1:]):
        sm.CreateLine(x0 * M, y_0 * M, 0, x1 * M, y_1 * M, 0)
    sm.CreateCenterLine(0, (y0 - 1) * M, 0, 0, (y0 + h + 1) * M, 0)
    sm.InsertSketch(True)
    f = fm.FeatureRevolve2(True, True, False, False, False, False, 0, 0, 6.283185307179586, 0, False, False, 0, 0,
                           0, 0, 0, True, True, True)
    assert f is not None, name
    f.Name = name


cylinder("Thread", THREAD_D, -THREAD_IN, CEIL + 0.01)
cylinder("Body", BODY_D, CEIL, CEIL + BODY_L)
dome("Glass", BODY_D, CEIL + BODY_L, GLASS_H)
sm.AddToDB = False
print("box", [round(v * 1000, 2) for v in model.GetPartBox(True)],
      f"expect Y -{THREAD_IN:.2f}..{CEIL + BODY_L + GLASS_H:.2f}, R {BODY_D / 2}")
e, w = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), e, w), e.value)
sw.CloseDoc(get(model, "GetTitle"))
