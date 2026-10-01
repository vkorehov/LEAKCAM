"""Build mechanical/jst/SPH-002T_wires.SLDPRT: the two SPH-002T-P0.5S crimps of a PHR-2 with their
wires, in the frame of JST's PHR-2.STEP (so it takes the housing's placement). Run on the SolidWorks
PC through the solidworks-mcp run_python tool (it provides sw, get, nothing, byref_int).

Our own model, dimensions from JST's drawing KRD-05353-4 (SPH-002T-P0.5S, 10:1) and catalog: from the
rear, insulation barrel 0..1.48, open link to 2.86, conductor barrel to 3.69, contact box to 5.7
(2.08 high, 1.5 wide). Crimped: the barrels wrapped round the wire (insulation) and the strands; the
wire lies on the base strip, its bottom 0.2 over the cavity floor. Wire #30-#24, insulation 0.8-1.5
(modelled 1.0). In the housing as JST's PHR-2 model has its cavities: X -3.25 / -1.25, floor Y 1.88,
rear face Z -3.43 (the crimp 0.13 inside it), pin windows in front at Z 2.38.
"""
M = 0.001
PATH = r"C:\hw\LEAKCAM\mechanical\jst\SPH-002T_wires.SLDPRT"
CAV_X, FLOOR, REAR = (-3.25, -1.25), 1.88, -3.43
Z0 = REAR + 0.13                                    # the crimp's rear end
INSUL, LINK, COND, BOX = 1.48, 2.86, 3.69, 5.7      # along it from the rear
BASE_T, BOX_W, BOX_H = 0.2, 1.5, 2.08
WIRE_D, STRANDS_D, WIRE_OUT = 1.0, 0.5, 12.0
WIRE_C = FLOOR + BASE_T + WIRE_D / 2                # wire centre height

model = sw.NewDocument(sw.GetUserPreferenceStringValue(8), 0, 0, 0)
sm, fm, ext = model.SketchManager, model.FeatureManager, model.Extension
sm.AddToDB = True


def front_boss(name, draw, z0, z1):      # Front Plane (u, v = X, Y, normal +Z), from Z z0 to z1
    model.ClearSelection2(True)
    assert ext.SelectByID2("Front Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    draw()
    sm.InsertSketch(True)
    f = fm.FeatureExtrusion3(True, False, False, 0, 0, (z1 - z0) * M, 0, False, False, False, False, 0, 0,
                             False, False, False, False, True, True, True, 3, abs(z0) * M, z0 < 0)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name


def rects(w, y0, y1):
    return lambda: [sm.CreateCornerRectangle((x - w / 2) * M, y0 * M, 0, (x + w / 2) * M, y1 * M, 0) for x in CAV_X]


def circles(d):
    return lambda: [sm.CreateCircleByRadius(x * M, WIRE_C * M, 0, d / 2 * M) for x in CAV_X]


front_boss("Insulation barrels", rects(BOX_W, FLOOR, WIRE_C + WIRE_D / 2 + 0.2), Z0, Z0 + INSUL)
front_boss("Base strips", rects(1.0, FLOOR, FLOOR + BASE_T), Z0 + INSUL - 0.01, Z0 + LINK + 0.01)
front_boss("Conductor barrels", rects(1.0, FLOOR, FLOOR + BASE_T + STRANDS_D + 0.2), Z0 + LINK, Z0 + COND)
front_boss("Contact boxes", rects(BOX_W, FLOOR, FLOOR + BOX_H), Z0 + COND - 0.01, Z0 + BOX)
front_boss("Wires", circles(WIRE_D), REAR - WIRE_OUT, Z0 + INSUL)
front_boss("Strands", circles(STRANDS_D), Z0 + INSUL - 0.01, Z0 + COND)
sm.AddToDB = False
print("box", [round(v * 1000, 2) for v in model.GetPartBox(True)])
e, w = byref_int(), byref_int()
print("saved:", ext.SaveAs(PATH, 0, 1, nothing(), e, w), e.value)
sw.CloseDoc(get(model, "GetTitle"))
