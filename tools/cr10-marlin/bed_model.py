"""CR-10 bed model for OrcaSlicer (printer preset bed_custom_model): the glass plate with its four
binder clips, so the plate view shows where they are. The check itself is the preset's
bed_exclude_area (the clips widened by the probe's and the fan duct's reach).

Machine frame: nozzle X 0..300, Y 0..300. Plate X -7.8..302.2, Y 0..310 (measured with the nozzle on
the left and back; 310 wide assumed). Clips 25 along the edge x 15 onto the plate x 2.5 high, on the
left and right edges, 40 from the front and back edges (user, 2026-11-14).
OrcaSlicer puts the model's origin at the bed centre: the model is written centred on X150 Y150.

  python bed_model.py OUT.stl
"""
import sys

from build123d import Align, Box, Pos, export_stl

PLATE_X, PLATE_Y, PLATE_T = (-7.8, 302.2), (0.0, 310.0), 3.0
CLIP_ALONG, CLIP_IN, CLIP_H, CLIP_FROM_END = 25.0, 15.0, 2.5, 40.0
CENTRE = (150.0, 150.0)


def box(x0, x1, y0, y1, z0, z1):
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=(Align.MIN, Align.MIN, Align.MIN))


def clips():
    out = []
    for x0 in (PLATE_X[0], PLATE_X[1] - CLIP_IN):
        for y0 in (PLATE_Y[0] + CLIP_FROM_END, PLATE_Y[1] - CLIP_FROM_END - CLIP_ALONG):
            out.append((x0, x0 + CLIP_IN, y0, y0 + CLIP_ALONG))
    return out


def model():
    m = box(*PLATE_X, *PLATE_Y, -PLATE_T, -0.1)
    for x0, x1, y0, y1 in clips():
        m += box(x0, x1, y0, y1, -0.2, CLIP_H)
    return Pos(-CENTRE[0], -CENTRE[1], 0) * m


if __name__ == "__main__":
    print("clips (x0, x1, y0, y1):", clips())
    export_stl(model(), sys.argv[1])
