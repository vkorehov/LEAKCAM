"""Clip-on heatsink shroud for the CR-10's E3D V6: a 40 x 40 x 10 fan at the front (+Z) blows
through the heatsink fins only; the floor closes below the fins, so no air reaches the heat break,
the block or the print. The clip wraps the heatsink from the fan side past its axis and snaps on.
It sits in the middle of the FANG duct (Printables 60691), which wraps left, right and back; the
geometry is built with the fan on -X and turned to the front at the end.

Frame (SolidWorks, as the reference assembly fan_duct.SLDASM): Y up, nozzle tip at the origin,
heatsink axis on Y, heater block facing the front (+Z, long side front-back), probe on the left.

  python shroud.py OUT_DIR      -> shroud.step (+ shroud.stl for slicing)
"""
import sys

from build123d import Box, Cylinder, Pos, RegularPolygon, Rot, export_step, export_stl, extrude

# E3D V6 (models/E3D_V6.step)
FIN_R = 11.15                  # fins, Y 19.6 .. 46.4
FIN_Y0, FIN_Y1 = 19.6, 46.4
BLOCK_X = 8.0                  # heater block half width (left-right), top at Y 17.5
SOCK_TOP = 19.0                # silicone sock top over the block

# fit (printer adds ~0.2 mm to features: holes print smaller)
CLIP_R = FIN_R + 0.1           # snug on the fins once printed
WALL = 1.6                     # walls >= 1.2 (fdm-printable-design)
CLIP_TO_X = 3.0                # the clip wraps past the axis to here: opening 21.7 mm < 22.3, it snaps

# fan 40 x 40 x 10, axis on X, back face at FAN_X
FAN, FAN_HOLES, FAN_HOLE_D = 40.0, 32.0, 3.4          # M3 clearance (prints ~3.2)
NUT_AF, NUT_GRIP = 5.9, 4.0    # M3 nut pocket across flats (5.5 + print allowance); plastic between fan face and nut
NUT_BOSS_R = 4.8               # boss around a screw crossing the duct: hex corner 3.4 + 1.4 wall
HEX_ROT = 0.0                  # flats toward the holder's top and bottom edges (upper pockets: 1 mm to the top)
FAN_X, FAN_Y = -17.0, 40.0                            # fan Y 20 .. 60, above the block and sock (the block is under the fan)
DUCT_Y0, DUCT_Y1 = SOCK_TOP + 0.8 + WALL, FIN_Y1          # air only onto the fins (floor and ceiling >= WALL)
RING_TOP = FIN_Y1 + WALL
WEB_Y0, WEB_Y1, WEB_Z0, WEB_Z1 = 20.5, 59.5, 5.0, 9.0     # the web to FANG along the fan holder's side edges
MOUNT_GAP = 3.75               # above the clip: room for the V6 mount clamp (r ~14, 15 wide) + 1 mm -> r 15


def shroud():
    half = FAN / 2 + 0.5
    # plenum: the block lies under it (fan at the front), so it starts above the sock everywhere
    y0 = SOCK_TOP + 0.8
    low = Pos((FAN_X - 10.5) / 2, (y0 + FAN_Y + FAN / 2) / 2, 0) * Box(-10.5 - FAN_X, FAN_Y + FAN / 2 - y0, 2 * half)
    high = Pos((-10.5 - 4.0) / 2, (y0 + FAN_Y + FAN / 2) / 2, 0) * Box(10.5 - 4.0, FAN_Y + FAN / 2 - y0, 2 * half)
    ring = Pos(0, (y0 + RING_TOP) / 2, 0) * Rot(90, 0, 0) * Cylinder(CLIP_R + WALL, RING_TOP - y0)
    keep = Pos((FAN_X + CLIP_TO_X) / 2, 30, 0) * Box(CLIP_TO_X - FAN_X, 80, 80)
    body = (low + high + ring) & keep
    # air: from the fan face to the fins, only between DUCT_Y0 and DUCT_Y1
    cav_x0 = FAN_X + WALL
    cavity = Pos((cav_x0 - 6.0) / 2, (DUCT_Y0 + DUCT_Y1) / 2, 0) * Box(-6.0 - cav_x0, DUCT_Y1 - DUCT_Y0, FAN - 2 * WALL)
    bore = Pos(0, 45, 0) * Rot(90, 0, 0) * Cylinder(CLIP_R, 60)
    fan_hole = Pos(FAN_X + WALL / 2, FAN_Y, 0) * Rot(0, 90, 0) * Cylinder(FAN / 2 - 1.0, WALL + 0.2)
    # above the clip: 3 mm free around the heatsink on the fan side, up to the top
    mount = (Pos(0, (RING_TOP + 70) / 2, 0) * Rot(90, 0, 0) * Cylinder(CLIP_R + MOUNT_GAP, 70 - RING_TOP)) & \
        (Pos(-30, 50, 0) * Box(52, 60, 60))
    body = body - cavity - bore - fan_hole - mount
    holes = [(FAN_Y + dy * FAN_HOLES / 2, dz * FAN_HOLES / 2) for dy in (-1, 1) for dz in (-1, 1)]
    back = -4.0                         # the holder's back face: every nut goes in from outside
    for y, z in holes:                  # a solid boss across the duct where a screw would cross it
        if DUCT_Y0 - NUT_BOSS_R < y < DUCT_Y1 + NUT_BOSS_R:
            body += Pos((FAN_X + WALL + back) / 2, y, z) * Rot(0, 90, 0) * Cylinder(NUT_BOSS_R, back - FAN_X - WALL)
    for y, z in holes:                  # M3 x 20 through the fan, nut in a hex pocket open at the back
        body -= Pos((FAN_X + back) / 2, y, z) * Rot(0, 90, 0) * Cylinder(FAN_HOLE_D / 2, back - FAN_X + 2)
        x0 = FAN_X + NUT_GRIP
        body -= Pos(x0, y, z) * Rot(0, 90, 0) * extrude(RegularPolygon(NUT_AF / 2, 6, major_radius=False, rotation=HEX_ROT), back + 0.5 - x0)
    return body


FANG_STL = __file__.rsplit("/", 1)[0] + "/models/fang_front.stl"


def fang_web(side):
    """[(y, x_end)] per 1 mm slice: the web reaches the FANG arm's inner wall face at every point of
    the slice (Z WEB_Z0..WEB_Z1) and stops >= 0.3 mm short of the wall's far side"""
    import struct
    import numpy as np
    d = open(FANG_STL, "rb").read(); n = struct.unpack("<I", d[80:84])[0]
    t = np.frombuffer(d[84:84 + n * 50], dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))["v"].reshape(-1, 3, 3).astype(float)
    v0, e1, e2 = t[:, 0], t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]

    def hits(y, z):
        o, dv = np.array([0.0, y, z]), np.array([float(side), 0, 0])
        h = np.cross(dv, e2); a = (e1 * h).sum(1); ok = np.abs(a) > 1e-9
        f = np.where(ok, 1 / np.where(ok, a, 1), 0); sv = o - v0; u = f * (sv * h).sum(1)
        q = np.cross(sv, e1); w = f * (q @ dv); tt = f * (e2 * q).sum(1)
        return np.sort(tt[ok & (u >= 0) & (u <= 1) & (w >= 0) & (u + w <= 1) & (tt > 0)])
    out = []
    for y in np.arange(WEB_Y0, WEB_Y1 + 0.01, 1.0):
        walls = [hits(yy, zz)[:2] for yy in (y - 0.5, y, y + 0.5) for zz in np.linspace(WEB_Z0, WEB_Z1, 5)]
        x_end = max(w[0] for w in walls) + 0.1
        if x_end > min(w[1] for w in walls) - 0.3:
            raise RuntimeError(f"web at Y {y}: wall too thin or uneven for a flat end ({x_end:.2f})")
        out.append((float(y), float(x_end)))
    return out


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    s = Rot(0, 90, 0) * shroud()     # fan side -X -> front (+Z)
    # webs that fuse the shroud to the FANG arms (models/fang_front.stl) along the whole side
    # edge: 1 mm slices from the side walls (X +-20.5) into the arms' inner wall, each ending
    # where it touches that slice's wall (ray-traced) and >= 0.3 mm short of its air channel
    for side in (-1, 1):
        for y, x_end in fang_web(side):
            s += Pos(side * (20.0 + x_end) / 2, y, (WEB_Z0 + WEB_Z1) / 2) * Box(x_end - 20.0, 1.0, WEB_Z1 - WEB_Z0)
    s = s.solid()                    # one solid, so it imports as a part
    b = s.bounding_box()
    print("shroud", [round(v, 2) for v in (b.min.X, b.min.Y, b.min.Z, b.max.X, b.max.Y, b.max.Z)], f"{s.volume:.0f} mm3")
    s.label = "shroud"
    export_step(s, f"{out}/shroud.step")
    export_stl(s, f"{out}/shroud.stl")
    # the print file: shroud + FANG in one STL (the slicer merges the overlapping pads)
    import struct
    a = open(f"{out}/shroud.stl", "rb").read(); f = open(FANG_STL, "rb").read()
    na, nf = struct.unpack("<I", a[80:84])[0], struct.unpack("<I", f[80:84])[0]
    open(f"{out}/fan_duct_fused.stl", "wb").write(b"shroud + FANG fused".ljust(80, b" ") + struct.pack("<I", na + nf) + a[84:] + f[84:])
    print("fused stl", na + nf, "triangles")
