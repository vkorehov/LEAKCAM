"""Turn the cover and its slicer modifiers into a print orientation, plain Python (runs on the PC too).

  python orient.py COVER.stl MODEL_MIN_X MODEL_MIN_Y MODEL_MIN_Z DOWN_X DOWN_Y DOWN_Z OUT_DIR

COVER.stl is SolidWorks' export of cover.SLDPRT; SolidWorks moves it into positive space, MODEL_MIN (mm, the part
box minimum from GetPartBox) moves it back to model coordinates. DOWN is the model direction that points at the bed.
Writes to OUT_DIR:
  cover_print.stl           the cover turned, centred on X/Y 0, standing on Z 0 (OrcaSlicer centres it on the bed)
  cover_fuzz_shell_plate.stl   fuzzy skin on: ~1 mm inside the outer form to 0.5 outside (cover_fuzz_shell.stl)
  cover_seam_band_plate.stl    fuzzy skin off: the base/cover split faces +-1.2 (cover_seam_band.stl), added after it
both in plate mm for a bed centre of (150, 150), as add_modifier takes them.
"""
import math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BED_CENTRE = (150.0, 150.0)


def read_stl(path):
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 80)[0]
    if 84 + 50 * n != len(b):
        raise ValueError(f"{path}: not a binary STL")
    return [struct.unpack_from("<9f", b, 84 + 50 * i + 12) for i in range(n)]


def write_stl(path, tris, name):
    out = bytearray(name.encode()[:80].ljust(80, b" ")) + struct.pack("<I", len(tris))
    for t in tris:
        a, b_, c = t[0:3], t[3:6], t[6:9]
        u = [b_[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        L = math.sqrt(sum(x * x for x in n)) or 1.0
        out += struct.pack("<12fH", *[x / L for x in n], *t, 0)
    open(path, "wb").write(out)


def rotation_to_bed(d):
    """the rotation that turns the unit vector d into (0, 0, -1) (Rodrigues)"""
    L = math.sqrt(sum(x * x for x in d))
    d = [x / L for x in d]
    t = [0.0, 0.0, -1.0]
    v = [d[1] * t[2] - d[2] * t[1], d[2] * t[0] - d[0] * t[2], d[0] * t[1] - d[1] * t[0]]
    c = sum(d[k] * t[k] for k in range(3))
    if c < -1 + 1e-9:                                   # d is +Z: half a turn about X
        return [[1, 0, 0], [0, -1, 0], [0, 0, -1]]
    K = [[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]]
    K2 = [[sum(K[i][k] * K[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return [[(i == j) + K[i][j] + K2[i][j] / (1 + c) for j in range(3)] for i in range(3)]


def apply(tris, R, shift):
    out = []
    for t in tris:
        q = []
        for k in range(3):
            p = t[3 * k:3 * k + 3]
            q += [sum(R[i][j] * p[j] for j in range(3)) + shift[i] for i in range(3)]
        out.append(q)
    return out


def main(cover, mx, my, mz, dx, dy, dz, out_dir):
    tris = read_stl(cover)
    lo = [min(t[k + 3 * j] for t in tris for j in range(3)) for k in range(3)]
    to_model = [float(m) - lo[k] for k, m in enumerate((mx, my, mz))]
    R = rotation_to_bed([float(dx), float(dy), float(dz)])
    turned = apply(tris, R, [0, 0, 0])
    turned = apply(turned, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [sum(R[i][j] * to_model[j] for j in range(3)) for i in range(3)])
    lo = [min(t[k + 3 * j] for t in turned for j in range(3)) for k in range(3)]
    hi = [max(t[k + 3 * j] for t in turned for j in range(3)) for k in range(3)]
    centre = [-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]]           # model -> print file
    write_stl(os.path.join(out_dir, "cover_print.stl"), apply(turned, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], centre), "cover print")
    plate = [centre[0] + BED_CENTRE[0], centre[1] + BED_CENTRE[1], centre[2]]
    for src, dst in (("cover_fuzz_shell.stl", "cover_fuzz_shell_plate.stl"), ("cover_seam_band.stl", "cover_seam_band_plate.stl")):
        write_stl(os.path.join(out_dir, dst), apply(apply(read_stl(os.path.join(HERE, src)), R, [0, 0, 0]),
                                                    [[1, 0, 0], [0, 1, 0], [0, 0, 1]], plate), dst)
    print("print box (mm):", [round(hi[k] - lo[k], 2) for k in range(3)], "plate offset:", [round(x, 3) for x in plate])


if __name__ == "__main__":
    main(*sys.argv[1:])
