"""Build mechanical/battery/LiPo_503450_1000mAh.SLDPRT: the vendor's model (LiPo_503450_1000mAh.step,
as downloaded) without its wire ends and without the yellow-wrapped protection-board block (6.0 thick,
thicker than the real cells), the pouch scaled to the cells as measured: 53.0 x 35.0 x 5.3. Run on the
SolidWorks PC through the solidworks-mcp run_python tool (it provides sw, get, typed, nothing, byref_int).

Vendor frame kept: length along X, width along Y centred, thickness along Z from 0; scaled about the
origin (pouch X -26.6..26.4, Y +-17.5, Z 0..5.3). The fit assembly places it with that.
"""
M = 0.001
STEP = r"C:\hw\LEAKCAM\mechanical\battery\LiPo_503450_1000mAh.step"
PATH = r"C:\hw\LEAKCAM\mechanical\battery\LiPo_503450_1000mAh.SLDPRT"
MEASURED = (53.0, 35.0, 5.3)
WIRES = ("solid4", "solid5", "solid6", "solid7")          # the wire ends in the vendor model

import os, tempfile
sw.CloseAllDocuments(True)
e, w = byref_int(), byref_int()
m = sw.LoadFile4(STEP, "r", sw.GetImportFileData(STEP) or nothing(), e)
tmp = os.path.join(tempfile.gettempdir(), "lipo_vendor.SLDPRT")          # bodies are reachable once reopened as a part
m.Extension.SaveAs(tmp, 0, 1, nothing(), e, w)
sw.CloseAllDocuments(True)
model = sw.OpenDoc6(tmp, 1, 1, "", e, w)
fm = model.FeatureManager
model.ClearSelection2(True)
gone = 0
for b_ in model.GetBodies2(0, False):
    bb = [v * 1000 for v in get(b_, "GetBodyBox")]
    if any(get(b_, "Name").endswith(s_) for s_ in WIRES) or bb[0] < -26.0:   # wire ends, the protection-board block
        b_.Select2(True, None); gone += 1
f = fm.InsertDeleteBody2(False)
if f:
    f.Name = "Wire ends and the protection-board block removed"
box = [v * 1000 for v in model.GetPartBox(True)]
size = [box[i + 3] - box[i] for i in range(3)]
k = [MEASURED[i] / size[i] for i in range(3)]
model.ClearSelection2(True)
for b_ in model.GetBodies2(0, False):
    b_.Select2(True, None)
f = fm.InsertScale(1, False, k[0], k[1], k[2])            # about the origin, per axis
if f is None:
    raise RuntimeError("scale failed")
f.Name = f"Scaled to the measured cell {MEASURED[0]} x {MEASURED[1]} x {MEASURED[2]}"
print("removed", gone, "bodies; pouch box", [round(v, 2) for v in box], "scale", [round(v, 4) for v in k])
print("box", [round(v * 1000, 2) for v in model.GetPartBox(True)])
print("saved:", model.Extension.SaveAs(PATH, 0, 1, nothing(), e, w), e.value)
sw.CloseAllDocuments(True)
