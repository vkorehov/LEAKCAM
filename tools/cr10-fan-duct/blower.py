"""Part-cooling blower for the CR-10 E3D V6: a 5015 radial fan upright on the right (+X) of the
hot end, outlet down, and the duct from its outlet to a slot that blows just below the nozzle tip.
The duct keeps >= 6 mm from the heater block (and its sock) and aims below the tip, so it cools the
print, not the block or the heat break.

Same frame as shroud.py: Y up, nozzle tip at the origin, heater block toward +Z, probe on the left.
The 5015 dimensions are a generic placeholder until the user's blower is measured (outlet!).

  python blower.py OUT_DIR      -> blower_5015.step (placeholder fan), part_duct.step
"""
import sys

from build123d import Box, Cylinder, Plane, Pos, Rectangle, Rot, Vector, export_step, loft

WALL = 1.6
# generic 5015 (u = width along -Z, v = height along Y, w = thickness along X)
B_W, B_H, B_T = 51.3, 51.3, 15.0
OUT_U0, OUT_U1 = 31.3, 51.3          # outlet, outer
OUT_IN = (17.0, 12.0)                # outlet opening (u x w)
INLET_D, HOLE_D = 28.0, 4.3
# placement
B_X0 = 15.6                          # blower faces X 15.6 .. 30.6: duct and fan >= 6 mm from the block side (X 8)
B_Y0 = 13.0                          # outlet face height; the duct runs from here down
# duct exit: a slot left of the block's right side, aimed just below the nozzle tip
EXIT_X, EXIT_Y = 17.2, 6.0           # centre of the exit slot (>= 6 mm from the block side, bottom edge >= 2 mm above the tip)
EXIT_W, EXIT_H = 14.0, 3.5           # slot width (along Z) and height
AIM = Vector(0, -1.5, 0)             # where the jet points: below the tip, at the print


def blower():
    """placeholder 5015 envelope: scroll housing + outlet block, inlet ring, two mounting holes"""
    zc = (OUT_U0 + OUT_U1) / 2       # outlet centred on Z = 0; the body extends to +Z (front)
    to_z = lambda u: zc - u
    body = Pos(B_X0 + B_T / 2, B_Y0 + B_H / 2 + 0.0, to_z(B_W / 2 - 2)) * Rot(0, 90, 0) * Cylinder(24.0, B_T)
    body += Pos(B_X0 + B_T / 2, B_Y0 + 13.0, to_z(zc)) * Box(B_T, 26.0, OUT_U1 - OUT_U0)
    body -= Pos(B_X0 + B_T, B_Y0 + B_H / 2, to_z(B_W / 2 - 2)) * Rot(0, 90, 0) * Cylinder(INLET_D / 2, 2.0)
    for u, v in ((4.5, B_H - 4.5), (B_W - 4.5, B_H - 4.5)):
        body -= Pos(B_X0 + B_T / 2, B_Y0 + v, to_z(u)) * Rot(0, 90, 0) * Cylinder(HOLE_D / 2, B_T + 1)
    return body


def section(centre, normal, x_dir, w, h):
    return Plane(origin=centre, z_dir=normal, x_dir=x_dir) * Rectangle(w, h)


def duct():
    """loft from the blower outlet (facing down) to the exit slot aimed at AIM"""
    top_c = Vector(B_X0 + B_T / 2, B_Y0, 0)
    exit_c = Vector(EXIT_X, EXIT_Y, 0)
    d = (AIM - exit_c).normalized()
    outer = loft([section(top_c, Vector(0, -1, 0), Vector(1, 0, 0), B_T + 2 * WALL, OUT_U1 - OUT_U0 + 2 * WALL),
                  section(exit_c, d, Vector(0, 0, 1).cross(d), EXIT_H + 2 * WALL, EXIT_W + 2 * WALL)])
    inner = loft([section(top_c + Vector(0, 0.5, 0), Vector(0, -1, 0), Vector(1, 0, 0), OUT_IN[1], OUT_IN[0]),
                  section(exit_c - d * 0.5, d, Vector(0, 0, 1).cross(d), EXIT_H, EXIT_W)])
    return outer - inner


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, s in (("blower_5015", blower()), ("part_duct", duct())):
        b = s.bounding_box()
        print(name, [round(v, 2) for v in (b.min.X, b.min.Y, b.min.Z, b.max.X, b.max.Y, b.max.Z)], f"{s.volume:.0f} mm3")
        s.label = name
        export_step(s, f"{out}/{name}.step")
