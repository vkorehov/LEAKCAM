"""rotate_cameras.py: turn both cameras in mechanical/enclosure/fit/fit.SLDASM 180 degrees about their lens axis
(X 0, Z -1.1; base frame), with their lenses and holder-to-enclosure screws: the rev 1 orientation (bugs/bug-005,
DESIGN.md "Camera orientation"). Run again to turn them back. Run on the SolidWorks PC through the solidworks-mcp
run_python tool, then mate_fit.py (its origin-plane mates take the parts where they are).
"""
ASM = r"C:\hw\LEAKCAM\mechanical\enclosure\fit\fit.SLDASM"
LENS_Z = -1.1
PARTS = ["Arducam_B0370_OV5647_M12-1", "Arducam_B0370_OV5647_M12-2", "M12_fisheye_222-1", "M12_fisheye_222-2"] + \
        [f"ISO7380_M2x5-{i}" for i in range(1, 9)]

sw.CloseAllDocuments(True)
e, w = byref_int(), byref_int()
m = sw.OpenDoc6(ASM, 2, 1, "", e, w)
asm = get(m, "GetTitle").rsplit(".", 1)[0]
C = {get(c, "Name2"): c for c in m.GetComponents(True)}
gone = []                                     # their mates first: a rebuild would pull them back
f = get(m, "FirstFeature")
while f:
    if get(f, "GetTypeName2") == "MateGroup":
        s = get(f, "GetFirstSubFeature")
        while s:
            n = get(s, "Name")
            if any(f" {p} " in f" {n} " for p in PARTS):
                gone.append(n)
            s = get(s, "GetNextSubFeature")
    f = get(f, "GetNextFeature")
m.ClearSelection2(True)                       # by the feature (SelectByID2 cannot select names with ':', mate_fit.py)
for n in gone:
    typed(m.FeatureByName(n), "IFeature").Select2(True, 0)
if gone:
    m.EditDelete()
m.ClearSelection2(True)
print(f"deleted {len(gone)} mates")
# about the axis X = 0, Z = LENS_Z (along Y): x -> -x, z -> 2 LENS_Z - z (metres)
turn = typed(transform([-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, -1.0], [0.0, 0.0, 2 * LENS_Z / 1000]), "IMathTransform")
for p in PARTS:
    c = C[p]
    if get(c, "IsFixed"):
        m.ClearSelection2(True)
        m.Extension.SelectByID2(f"{p}@{asm}", "COMPONENT", 0, 0, 0, False, 0, nothing(), 0)
        m.UnfixComponent()
    b0 = [round(v * 1000, 2) for v in c.GetBox(False, False)]
    c.Transform2 = typed(get(c, "Transform2"), "IMathTransform").Multiply(turn)
    print(p, b0, "->", [round(v * 1000, 2) for v in c.GetBox(False, False)])
get(m, "EditRebuild3")
z = [round(v * 1000, 2) for v in C[PARTS[0]].GetBox(False, False)][2]
if abs(z - (-9.18)) > 0.05 and abs(z - (-17.6)) > 0.05:
    raise RuntimeError(f"unexpected camera position after the rebuild: Z from {z}")
print("after the rebuild, bottom camera from Z", z)
print("saved", m.Save3(1, e, w), e.value)
