"""Seam lip and its snap tabs, shared by build_base.py and build_cover.py (prepended like led_strips.py).

The cover's lip runs round the whole seam inside the walls' thickness, its inner face flush with the
pocket, and sits in a rebate in the base's wall tops: the walls cannot wobble. Designed with 0.2
clearance (the printer adds about 0.2), printed it is tight. It is broken only where something
crosses the seam (the cover passes the gaps: the probe wires, the J4 flex riser).

Eight mini snap clips are the lip itself, taller at those places: one piece with the lip and the wall,
printed continuously. A bead on each clip's outer face snaps into a groove in the base's wall, the
clip bending inwards into a flex room (the usual cantilever-and-bead click). Four at the ends (thick end
walls, 0.3 beads: X +-13 at the camera end beside the J4 flex riser, X +-4 at the antenna end between
the screws), four on the long sides (1.1 walls: 0.15 beads, 0.3 wall left at the
groove). Opening: a knife in the seam and force. Four M2x20 thread-forming screws from the base's
bottom hold it for good: heads in counterbores under the J4 flex loop's floor (Y -10), clearance holes
up through the base, pilots in the cover. The same X at both ends, where the bottom is flat (|X| <
12.19, inside the LED chamfers) and clear of the flex loop and riser; 1 mm of plastic from the
counterbores to the end faces.

Profiles of the end clips are drawn on the Right Plane (sketch u, v = model -Z, Y) and extruded across
X, of the side clips on the Front Plane (u, v = model X, Y) along Z; lip and rebate on the Top Plane
(sketch x, y = model X, -Z).
"""
import math
LIP_CLEAR = 0.2
SEAM_Z = (-33.6, 32.8)                          # the lip's inner outline at the ends: the cover's cell pocket end (camera),
                                                # the base's board pocket end (antenna); the sides at the pockets' X +-18.45
REBATE_T, REBATE_H = 0.65, 1.0                  # base rebate: width into the wall, depth below the seam
LIP_T, LIP_H = REBATE_T - LIP_CLEAR, REBATE_H - LIP_CLEAR
TAB_W, TAB_H = 6.0, 3.0                         # snap clips: width, height below the seam
TAB_X = {-1: (-13.0, 13.0), 1: (-4.0, 4.0)}     # end clips' X centres: camera end, antenna end
BEAD = 0.3                                      # bead height over the end clips' face
TAB_Z, TAB_W_SIDE, BEAD_SIDE = (-26.4, 7.5), 3.8, 0.15   # side clips (both sides): Z centres, width, bead; clear of
                                                # USB-C notches, S1-S3 plugs and the mic slot
FLEX = 0.5                                      # room for the tab to bend inwards
SEAM_SCREWS = [(x, z) for z in (-36.6, 34.4) for x in (-10.2, 10.2)]   # X, Z: counterbores to X 12.1, 1.0 from the ends
SCREW_SEAT, SCREW_L, HEAD_H = -10.0, 20.0, 1.1  # counterbore floor (below the flex loop, Y -9.5), M2x20 button head
SCREW_CB, SCREW_HOLE, SCREW_PILOT = 1.9, 1.05, 0.8  # radii (the -X hole 0.05 off the flex loop at X -9.1)


def tp_feature(name, rects, y0, y1, boss, expect, circles=()):
    """rectangles (x0, z0, x1, z1) and circles (x, z, r) on the Top Plane, from Y y0 up to y1: a boss
    or a cut (uses the build script's model, sm, fm, ext, fbox)"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for x0, z0, x1, z1 in rects:
        sm.CreateCornerRectangle(x0 * M, -z0 * M, 0, x1 * M, -z1 * M, 0)
    for x, z, r in circles:
        sm.CreateCircleByRadius(x * M, -z * M, 0, r * M)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(y0) * M, y0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (y1 - y0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(y0) * M, y0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def rp_feature(name, zy, x0, x1, boss, expect):
    """polygon (Z, Y) on the Right Plane, extruded through X x0..x1: a boss or a cut"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Right Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    pts = [(-z, y) for z, y in zy]
    for (a, b), (c, d) in zip(pts, pts[1:] + pts[:1]):
        sm.CreateLine(a * M, b * M, 0, c * M, d * M, 0)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(x0) * M, x0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(x0) * M, x0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def fp_feature(name, xy, z0, z1, boss, expect):
    """polygon (X, Y) on the Front Plane, extruded through Z z0..z1: a boss or a cut"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Front Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    for (a, b), (c, d) in zip(xy, xy[1:] + xy[:1]):
        sm.CreateLine(a * M, b * M, 0, c * M, d * M, 0)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (z1 - z0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(z0) * M, z0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (z1 - z0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(z0) * M, z0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def clips(pocket_x, z_cam, z_ant):
    """each clip: name, the profile maker (polygon in its plane from (w, y) points), w of the lip's inner
    face, bead, extrusion span; w is outwards (along Z at the ends, along X on the sides)"""
    out = []
    for e, w_in in ((-1, -z_cam), (1, z_ant)):
        for xc in TAB_X[e]:
            out.append((f"{'camera' if e < 0 else 'antenna'} end X {xc:+.0f}", rp_feature,
                        lambda pts, e=e: [(e * w, y) for w, y in pts], w_in, BEAD, (xc - TAB_W / 2, xc + TAB_W / 2)))
    for s in (-1, 1):
        for zc in TAB_Z:
            out.append((f"{'+X' if s > 0 else '-X'} side Z {zc:+.1f}", fp_feature,
                        lambda pts, s=s: [(s * w, y) for w, y in pts], pocket_x, BEAD_SIDE, (zc - TAB_W_SIDE / 2, zc + TAB_W_SIDE / 2)))
    return out


def ring(pocket_x, z_cam, z_ant, t, inset=0.0):
    """outer and inner rectangle of the band t wide outside (X +-pocket_x, Z z_cam..z_ant)"""
    return [(-pocket_x - t, z_cam - t, pocket_x + t, z_ant + t),
            (-pocket_x + inset, z_cam + inset, pocket_x - inset, z_ant - inset)]


def tab_profile(w_in, bead):
    """the clip with its bead as (w, y) points"""
    w_out = w_in + LIP_T
    bot = PARTING - TAB_H
    return [(w_in, PARTING + 0.01), (w_out, PARTING + 0.01), (w_out, bot + 1.4), (w_out + bead, bot + 0.9),
            (w_out + bead, bot + 0.7), (w_out, bot + 0.2), (w_out, bot), (w_in, bot)]


def seam_cover_features(pocket_x, z_cam, z_ant, side_gaps, end_gaps):
    """side_gaps {side: [(z0, z1)]}, end_gaps {end: [(x0, x1)]}: where the lip must stay open"""
    tp_feature("Seam lip", ring(pocket_x, z_cam, z_ant, LIP_T), PARTING - LIP_H, PARTING + 0.01, True,
               f"{LIP_T} x {LIP_H} round the seam")
    gaps = [((pocket_x - 0.1, z0, pocket_x + LIP_T + 0.1, z1) if s > 0 else (-pocket_x - LIP_T - 0.1, z0, -pocket_x + 0.1, z1))
            for s, zs in side_gaps.items() for z0, z1 in zs]
    gaps += [((x0, z_ant - 0.1, x1, z_ant + LIP_T + 0.1) if e > 0 else (x0, z_cam - LIP_T - 0.1, x1, z_cam + 0.1))
             for e, xs in end_gaps.items() for x0, x1 in xs]
    tp_feature("Seam lip gaps", gaps, PARTING - LIP_H - 0.1, PARTING, False, f"{len(gaps)}: what crosses the seam")
    for name, feat, plane, w_in, bead, (a0, a1) in clips(pocket_x, z_cam, z_ant):
        feat(f"Snap clip {name}", plane(tab_profile(w_in, bead)), a0, a1, True, f"{TAB_H} below the seam, bead {bead}")


def seam_base_features(pocket_x, z_cam, z_ant, pocket_end=32.8):
    """pocket_end: |Z| of the base's board pocket ends; a clip's flex room merges into it rather than
    leave a wall under 0.6"""
    tp_feature("Seam rebate", ring(pocket_x, z_cam, z_ant, REBATE_T, inset=0.01), PARTING - REBATE_H, PARTING + 0.1, False,
               f"{REBATE_T} x {REBATE_H} round the seam: the cover's lip, no wobble")
    bot = PARTING - TAB_H - LIP_CLEAR
    gy = PARTING - TAB_H
    for name, feat, plane, w_in, bead, (a0, a1) in clips(pocket_x, z_cam, z_ant):
        w_wall = w_in + REBATE_T
        a0, a1 = a0 - LIP_CLEAR, a1 + LIP_CLEAR
        w0 = w_in - FLEX
        if feat is rp_feature and 0 < w0 - pocket_end < 0.6:      # the camera end: no 0.3 ledge to the pocket
            w0 = pocket_end - 0.01
        feat(f"Snap clip pocket {name}", plane([(w0, bot), (w_wall, bot), (w_wall, PARTING + 0.1),
                                                (w0, PARTING + 0.1)]),
             a0, a1, False, f"the clip and {FLEX} flex room, from Y {bot:.1f}")
        w_deep = w_in + LIP_T + bead + LIP_CLEAR
        feat(f"Snap groove {name}", plane([(w_wall - 0.01, gy + 0.5), (w_deep, gy + 0.5), (w_deep, gy + 1.1),
                                           (w_wall - 0.01, gy + 1.1 + (w_deep - w_wall + 0.01))]),   # 45 deg top: no ledge
             a0, a1, False, f"catches the bead, wall left {abs(w_in) + 1.1 - (w_in + LIP_T + bead + LIP_CLEAR):.2f}" if feat is fp_feature
             else "catches the bead")


def seam_screw_base_features(bottom):
    tp_feature("Seam screw counterbores", [], bottom - 0.1, SCREW_SEAT, False, f"from the bottom to Y {SCREW_SEAT}",
               circles=[(x, z, SCREW_CB) for x, z in SEAM_SCREWS])
    tp_feature("Seam screw holes", [], SCREW_SEAT - 0.01, PARTING + 0.1, False, "clearance up through the base",
               circles=[(x, z, SCREW_HOLE) for x, z in SEAM_SCREWS])


def seam_screw_cover_features():
    top = SCREW_SEAT + SCREW_L + 0.7
    tp_feature("Seam screw pilots", [], PARTING - 0.1, top, False, f"thread-forming, Y {PARTING}..{top:.1f}",
               circles=[(x, z, SCREW_PILOT) for x, z in SEAM_SCREWS])


def seam_screws():
    """(axis rows, head-top point mm) of the four screws, for the assembly"""
    return [([0, 1, 0, 1, 0, 0, 0, 0, -1], (x, SCREW_SEAT - HEAD_H, z)) for x, z in SEAM_SCREWS]


# --- the probe wires' plugs: JST PHR-2 (SPH-002T-P0.5S crimps) in the S2B-PH-K-S side-entry headers S1-S3.
# Seated, a PHR-2 ends 8.26 past its header's pins (JST models: the pins' board legs 1.35 in front of
# the header's back, the plug's front on the header's stop 2.75 further, the plug 6.86 long), which is
# inside the 1.1 side wall: its rear flange (5.8 wide, 4.5 high from 0.3 over the board back) sits in a
# pocket in the wall, under a 0.5 bulge on the outside (bevelled 30 deg top and bottom: no supports;
# kept small, next to the USB-C cable plugs).
# The wires lie on the crimps' base strips (JST KRD-05353-4: the insulation barrel on the cavity floor,
# 1.48 below the pin axis, the wire's bottom 0.2 over it) and run straight out through U-slots in the
# base whose floor is at that height, so any insulation 0.8-1.5 leaves straight. Outside, a gland per
# plug (split by the seam, 30 deg faces to print without supports, R0.4 corners) carries the slots on
# 2.6 further; the cover's half lies over the wires, and a tongue per wire drops into the slot over
# its whole length (wall and gland): its saddle is the wire's own top arc (insulation 1.0) 0.2 lower,
# so it presses the wire round its upper half all along. The wires are the device's legs:
# they bend down out of the gland over an R0.6 round on the slot floor (no kink), the tongue's tip
# is rounded too; the cover's half takes the load the legs push up with.
PLUGS = [(1, 10.972, -20.567), (1, 10.8, 17.5), (-1, -10.8, -1.5)]   # S1, S3, S2: wall side, pins X, Z middle
PLUG_REACH, PLUG_W, PLUG_Y = 8.26, 5.8, (1.895, 6.395)               # seated rear past the pins, flange, Y span
PLUG_PITCH, WIRE_FLOOR, WIRE_SLOT = 2.0, 3.405, 1.2                  # wires: Z pitch, bottom height, slot width (1.0 wire)
BULGE, PLUG_CLEAR = 0.5, 0.2
GLAND_L, GLAND_W, GLAND_R = 2.6, 5.2, 0.4                         # gland: length out of the bulge, width, corners
GLAND_UNDER, BEND_R, TIP_R = 1.0, 0.6, 0.4                        # plastic under the slots, the exit's rounds
SLOPE = math.tan(math.radians(30))                                # bulge and gland faces: 30 deg (prints, clears USB-C plugs)
BULGE_MARGIN = 0.2                                                # bulge past the flange pocket in Z
WIRE_D, GRIP = 1.0, 0.2                                            # wire (insulation), press of the saddles


def plug_rear(pins_x, side):
    return pins_x + side * PLUG_REACH


def plug_pocket(side, pins_x, zc, outer_x):
    """XY of the flange pocket (side-signed X), Z span"""
    x1 = abs(plug_rear(pins_x, side)) + PLUG_CLEAR
    return (side * (outer_x - 1.2), side * x1), (zc - PLUG_W / 2 - PLUG_CLEAR, zc + PLUG_W / 2 + PLUG_CLEAR)


def plug_gaps(pocket_x):
    """the lip's gaps over the plugs, {side: [(z0, z1)]}"""
    out = {}
    for side, px, zc in PLUGS:
        out.setdefault(side, []).append((zc - PLUG_W / 2 - PLUG_CLEAR - 0.1, zc + PLUG_W / 2 + PLUG_CLEAR + 0.1))
    return out


def bulge_profile(side, outer_x, y0, y1, bevel_lo, bevel_hi):
    """XY polygon of a bulge between y0 and y1, bevelled 45 deg where asked"""
    xi, xo = side * (outer_x - 0.01), side * (outer_x + BULGE)
    return [(xi, y0 - (BULGE * SLOPE if bevel_lo else 0)), (xo, y0), (xo, y1), (xi, y1 + (BULGE * SLOPE if bevel_hi else 0))]


def rp_circle(name, z, y, r, x0, x1, boss, expect):
    """a circle (Z, Y) on the Right Plane, extruded through X x0..x1"""
    model.ClearSelection2(True)
    assert ext.SelectByID2("Right Plane", "PLANE", 0, 0, 0, False, 0, nothing(), 0)
    sm.InsertSketch(True)
    sm.CreateCircleByRadius(-z * M, y * M, 0, r * M)
    sm.InsertSketch(True)
    if boss:
        f = fm.FeatureExtrusion3(True, False, False, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                                 False, False, False, False, True, True, True, 3, abs(x0) * M, x0 < 0)
    else:
        f = fm.FeatureCut4(True, False, True, 0, 0, (x1 - x0) * M, 0, False, False, False, False, 0, 0,
                           False, False, False, False, False, True, True, False, False, False,
                           3, abs(x0) * M, x0 < 0, False)
    if f is None:
        raise RuntimeError(f"{name}: feature failed")
    f.Name = name
    print(f"{name:26s} {fbox(f)}  expect {expect}")


def gland(part, side, zc, nm, outer_x):
    """the gland's half below (base) or above (cover) the seam"""
    x0, x1 = outer_x + BULGE - 0.01, outer_x + BULGE + GLAND_L
    hw, r = GLAND_W / 2, GLAND_R
    sx = lambda a, b: (side * a, side * b) if side > 0 else (side * b, side * a)
    ys = (WIRE_FLOOR - GLAND_UNDER - (x1 - x0) * SLOPE - 0.1, PARTING) if part == "base" else (PARTING, PARTING + 0.6 + (x1 - x0) * SLOPE + 0.2)
    for i, (xa, xb, w) in enumerate(((x0, x1 - r, hw), (x0, x1, hw - r))):
        a, b = sx(xa, xb)
        tp_feature(f"Gland {nm} {i}", [(a, zc - w, b, zc + w)], *ys, True, "")
    tp_feature(f"Gland corners {nm}", [], *ys, True, f"R{r}", circles=[(side * (x1 - r), zc + k * (hw - r), r) for k in (-1, 1)])
    # the 30 deg face: under the base half, over the cover half (each prints without supports)
    if part == "base":
        lo = WIRE_FLOOR - GLAND_UNDER                           # under the slots' floor at the gland's end
        tri = [(x0, lo - (x1 - x0) * SLOPE), (x1 + 0.2, lo + 0.2 * SLOPE), (x1 + 0.2, ys[0] - 0.5), (x0, ys[0] - 0.5)]
    else:
        hi = PARTING + 0.6                                      # 0.6 over the seam at the gland's end
        tri = [(x0, hi + (x1 - x0) * SLOPE), (x1 + 0.2, hi - 0.2 * SLOPE), (x1 + 0.2, ys[1] + 0.5), (x0, ys[1] + 0.5)]
    fp_feature(f"Gland face {nm}", [(side * x, y) for x, y in tri], zc - hw - 0.1, zc + hw + 0.1, False, "30 deg")


def exit_round(outer_x, y, r, d):
    """(|X|, Y) polygon cutting a quarter round of radius r on the edge at the gland's end, at height y,
    turning down (d -1, the slot floor) or up (d +1, the tongue's tip)"""
    x1 = outer_x + BULGE + GLAND_L
    cx, cy = x1 - r, y + d * r
    arc = [(cx + r * math.sin(math.radians(t)), cy - d * r * math.cos(math.radians(t))) for t in range(0, 91, 10)]
    return arc + [(x1 + 0.3, cy), (x1 + 0.3, y - d * 0.1), (cx, y - d * 0.1)]


def plug_features(part, outer_x):
    """part 'base' (below the seam) or 'cover' (above it)"""
    lo, hi = PLUG_Y[0] - PLUG_CLEAR, PLUG_Y[1] + PLUG_CLEAR
    for side, px, zc in PLUGS:
        nm = f"{'+X' if side > 0 else '-X'} Z {zc:+.1f}"
        (xa, xb), (z0, z1) = plug_pocket(side, px, zc, outer_x)
        zb = (z0 - BULGE_MARGIN, z1 + BULGE_MARGIN)               # the wall beside the pocket holds its sides
        if part == "base":
            fp_feature(f"Plug bulge {nm}", bulge_profile(side, outer_x, lo, PARTING, True, False), *zb, True,
                       f"{BULGE} out, wall behind the flange {outer_x + BULGE - abs(plug_rear(px, side)) - PLUG_CLEAR:.2f}")
            ys = (lo, PARTING + 0.1)
        else:
            fp_feature(f"Plug bulge {nm}", bulge_profile(side, outer_x, PARTING, hi, False, True), *zb, True,
                       f"{BULGE} out")
            ys = (PARTING - 0.1, hi)
        fp_feature(f"Plug flange pocket {nm}", [(xa, ys[0]), (xb, ys[0]), (xb, ys[1]), (xa, ys[1])], z0, z1, False,
                   f"the PHR-2's rear to X {plug_rear(px, side):.2f}, {PLUG_CLEAR} clear")
        gland(part, side, zc, nm, outer_x)
        if part == "cover":                                     # a tongue with a saddle on each wire
            wire_c = WIRE_FLOOR + WIRE_D / 2
            for zw in (zc - PLUG_PITCH / 2, zc + PLUG_PITCH / 2):
                ta, tb = sorted((side * (abs(xb) + 0.07), side * (outer_x + BULGE + GLAND_L)))
                tw = WIRE_SLOT / 2 - PLUG_CLEAR
                r = WIRE_D / 2
                yb = wire_c - GRIP + math.sqrt(r * r - tw * tw) - 0.05     # under the saddle's edges
                tp_feature(f"Wire tongue {nm} {zw:+.2f}", [(ta, zw - tw, tb, zw + tw)],
                           yb, PARTING + 0.01, True, "into the base's slot, over the whole length")
                rp_circle(f"Wire saddle {nm} {zw:+.2f}", zw, wire_c - GRIP, r,
                          ta - 0.1, tb + 0.1, False, f"the wire's arc, {GRIP} into it, below the seam")
                fp_feature(f"Wire tongue tip {nm} {zw:+.2f}", [(side * x, y) for x, y in exit_round(outer_x, yb, TIP_R, 1)],
                           zw - WIRE_SLOT / 2, zw + WIRE_SLOT / 2, False, f"R{TIP_R}")
        if part == "base":
            xw = (xa, side * (outer_x + BULGE + GLAND_L + 0.2))
            for zw in (zc - PLUG_PITCH / 2, zc + PLUG_PITCH / 2):
                fp_feature(f"Probe wire slot {nm} {zw:+.2f}", [(xw[0], WIRE_FLOOR - 0.1), (xw[1], WIRE_FLOOR - 0.1),
                                                               (xw[1], PARTING + 0.1), (xw[0], PARTING + 0.1)],
                           zw - WIRE_SLOT / 2, zw + WIRE_SLOT / 2, False, f"U-slot from Y {WIRE_FLOOR - 0.1:.2f}, the cover closes it")
                fp_feature(f"Wire bend {nm} {zw:+.2f}", [(side * x, y) for x, y in exit_round(outer_x, WIRE_FLOOR - 0.1, BEND_R, -1)],
                           zw - WIRE_SLOT / 2, zw + WIRE_SLOT / 2, False, f"R{BEND_R} on the slot floor at the exit")
