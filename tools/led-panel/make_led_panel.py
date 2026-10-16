"""The four LED strips as their own 2-layer panel, cut out of the main board's fab outputs.

In rev 1 the strips are panelled with the main board (MIFA) and cut off by hand along silkscreen lines: four
5.0 x 37.0 strips at X 0-5, 6-11, 12-17, 18-23 and Y 28-65 (1 mm saw gaps). They use the outer layers only (the
inner layers G1-G4 are empty there), so a strip panel is a plain copy of their outer-layer objects:

  cell      5.6 x 37.2 per strip, centred on its copper: the size of the boards as made (the enclosure's seats fit
            it) and V-cuts 0.67 / 0.45 mm from the nearest copper (the 5.0 x 37.0 lines are 0.37 / 0.35 from it)
  panel     the 4 cells side by side, 5 mm rails at both ends: 22.4 x 47.2; V-cuts between the cells and at the
            rails, all edge to edge; 3 fiducials on the rails
  layers    top/bottom copper, mask, silkscreen, top paste (all parts are on top; the bottom paste over the wire
            pads is left out so no bottom side is planned), drills; outline and V-cut lines in their own files
  BOM, CPL  the 16 LEDs, positions moved with their strips, rotations from PickPlace_MIFA.csv (JLC-corrected)

  python make_led_panel.py [OUT_DIR]      (needs gerbonara)
"""
import copy, csv, os, sys, zipfile
import gerbonara as g
from gerbonara.apertures import CircleAperture
from gerbonara.graphic_objects import Flash, Line
from gerbonara.utils import MM

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "Project Outputs for LEAKCAM")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "Project Outputs for LEDS panel")

STRIPS = [(6.0 * k, 6.0 * k + 5.0) for k in range(4)]   # X of each strip in the MIFA panel
SY0, SY1 = 28.0, 65.0                                   # Y of the strips
CELL_W, CELL_L, RAIL = 5.6, 37.2, 5.0
PANEL_W, PANEL_L = 4 * CELL_W, CELL_L + 2 * RAIL
FIDUCIALS = [(3.0, 2.5), (PANEL_W - 3.0, 2.5), (3.0, PANEL_L - 2.5)]
LAYERS = [("MIFA.GTL", "LEDS.GTL"), ("MIFA.GBL", "LEDS.GBL"), ("MIFA.GTS", "LEDS.GTS"), ("MIFA.GBS", "LEDS.GBS"),
          ("MIFA.GTO", "LEDS.GTO"), ("MIFA.GBO", "LEDS.GBO"), ("MIFA.GTP", "LEDS.GTP")]


def shift(k):
    """MIFA -> panel offset of strip k: its 5.0 x 37.0 area centred in its 5.6 x 37.2 cell"""
    return (CELL_W * k + (CELL_W - 5.0) / 2 - STRIPS[k][0], RAIL + (CELL_L - 37.0) / 2 - SY0)


def inside(o, k):
    (a, b), (c, d) = o.bounding_box(unit=MM)
    x0, x1 = STRIPS[k]
    return a >= x0 - 0.01 and c <= x1 + 0.01 and b >= SY0 - 0.01 and d <= SY1 + 0.01


def copy_layer(src, cls):
    """the strips' objects of one MIFA layer, each moved into its cell (the silkscreen cut lines cross the strip
    bounds and stay behind)"""
    f = cls.open(os.path.join(SRC, src))
    out = copy.copy(f)
    out.objects = []
    for k in range(4):
        dx, dy = shift(k)
        for o in f.objects:
            if inside(o, k):
                o2 = copy.deepcopy(o)
                o2.offset(dx, dy, MM)
                out.objects.append(o2)
    return out


def lines_file(segments, width=0.1):
    f = g.GerberFile()
    ap = CircleAperture(width, unit=MM)
    for (x1, y1), (x2, y2) in segments:
        f.objects.append(Line(x1, y1, x2, y2, aperture=ap, unit=MM))
    return f


def main():
    os.makedirs(OUT, exist_ok=True)
    made = []
    for src, dst in LAYERS:
        layer = copy_layer(src, g.GerberFile)
        if dst in ("LEDS.GTL", "LEDS.GTS"):                  # fiducials: 1 mm copper, 2 mm mask opening
            ap = CircleAperture(1.0 if dst == "LEDS.GTL" else 2.0, unit=MM)
            layer.objects += [Flash(x, y, aperture=ap, unit=MM) for x, y in FIDUCIALS]
        print(f"{dst}: {len(layer.objects)} objects")
        if not layer.objects:                                 # nothing there (the bottom silkscreen): no file
            continue
        layer.save(os.path.join(OUT, dst))
        made.append(dst)
    drill = copy_layer("MIFA-RoundHoles.TXT", g.ExcellonFile)
    drill.save(os.path.join(OUT, "LEDS.TXT"))
    made.append("LEDS.TXT")
    print(f"LEDS.TXT: {len(drill.objects)} holes")
    W, L = PANEL_W, PANEL_L
    lines_file([((0, 0), (W, 0)), ((W, 0), (W, L)), ((W, L), (0, L)), ((0, L), (0, 0))]).save(os.path.join(OUT, "LEDS.GKO"))
    vcuts = [((CELL_W * k, 0), (CELL_W * k, L)) for k in (1, 2, 3)] + [((0, y), (W, y)) for y in (RAIL, RAIL + CELL_L)]
    lines_file(vcuts).save(os.path.join(OUT, "LEDS-VCUT.GM1"))
    made += ["LEDS.GKO", "LEDS-VCUT.GM1"]
    with zipfile.ZipFile(os.path.join(OUT, "LEDS_panel_gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for f in made:
            z.write(os.path.join(OUT, f), f)

    # BOM + CPL: the LEDs from the JLC-corrected MIFA CPL, moved with their strips
    rows = [r for r in csv.DictReader(open(os.path.join(REPO, "PickPlace_MIFA.csv"))) if r["Designator"].startswith("LED")]
    parts = {}
    for r in csv.DictReader(open(os.path.join(REPO, "BOM_JLCPCB.csv"))):
        for d in r["Designator"].split(","):
            parts[d.strip()] = r
    with open(os.path.join(OUT, "LEDS_panel_CPL.csv"), "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            x, y = float(r["Mid X"].rstrip("m")), float(r["Mid Y"].rstrip("m"))
            k = next(i for i, (x0, x1) in enumerate(STRIPS) if x0 <= x <= x1)
            dx, dy = shift(k)
            w.writerow([r["Designator"], f"{x + dx:.4f}mm", f"{y + dy:.4f}mm", r["Layer"], r["Rotation"]])
    leds = sorted({d for d in (r["Designator"] for r in rows)}, key=lambda d: int(d[3:]))
    with open(os.path.join(OUT, "LEDS_panel_BOM.csv"), "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["Comment", "Designator", "Footprint", "JLCPCB Part #（optional）", "Manufacturer Part"])
        for comment in sorted({parts[d]["Comment"] for d in leds}):
            ds = [d for d in leds if parts[d]["Comment"] == comment]
            p = parts[ds[0]]
            w.writerow([comment, ",".join(ds), p["Footprint"], p["JLCPCB Part #（optional）"], comment])
    print(f"CPL/BOM: {len(rows)} LEDs; panel {W:.1f} x {L:.1f} mm -> {OUT}")


if __name__ == "__main__":
    main()
