"""Build mechanical/camera/M12_fisheye_222.SLDPRT: the cameras' 222 degree M12 fisheye (EXTERNAL_PARTS.md,
part 4), screwed in from outside after assembly through the enclosure's lens thread hole. Run on the
SolidWorks PC through the solidworks-mcp run_python tool (it provides sw, get, nothing, byref_int).

No drawing in the listing: the body and front element are estimated from its photo (the body about
0.6 of the 25 mm board); measure the lens and update BODY_D, BODY_L, GLASS_*.

Part frame: axis +Y outwards, origin on the holder's top face (where the enclosure's barrel pocket
ends); the thread runs THREAD_IN into the holder and through the CEIL wall, the body stands on the
enclosure surface.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\camera\M12_fisheye_222.SLDPRT"
THREAD_D, THREAD_IN = 12.0, 3.5                 # M12 x 0.5, engaged this far into the holder
CEIL = 1.82                                     # enclosure wall over the holder (build_base.py, build_cover.py)
BODY_D, BODY_L = 15.0, 8.0                      # estimated
GLASS_D, GLASS_H = 11.0, 1.5                    # front element proud of the body, estimated

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


cylinder("Thread", THREAD_D, -THREAD_IN, CEIL + 0.01)
cylinder("Body", BODY_D, CEIL, CEIL + BODY_L)
cylinder("Front element", GLASS_D, CEIL + BODY_L - 0.01, CEIL + BODY_L + GLASS_H)
sm.AddToDB = False
print("box", [round(v * 1000, 2) for v in model.GetPartBox(True)],
      f"expect Y -{THREAD_IN}..{CEIL + BODY_L + GLASS_H:.2f}, R {BODY_D / 2}")
e, w = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), e, w), e.value)
sw.CloseDoc(get(model, "GetTitle"))
