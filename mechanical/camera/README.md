# Camera model

`Arducam_B0370_OV5647_M12.step`: Arducam B0370, an OV5647 board in the Raspberry Pi camera v1.3
format with an M12 lens holder, from UCTRONICS
(https://www.uctronics.com/download/Mechanical_Drawing/B0370.STEP, downloaded 2026-09-30, kept here
so it is not fetched again). The LEAKCAM cameras (EXTERNAL_PARTS.md, part 4: Aideepen OV5647, 222
degree) are the same board format: 25 x 24 mm, four holes 2 mm in from the edges on 21 x 12.5 mm,
15-pin FFC connector on the lens side at one short edge, M12 x 0.5 holder.

Not the real lens: B0370's lens is a 14 x 14 x 9 mm low-distortion one. The 222 degree fisheye has a
larger front element; its diameter and height come from measuring the part.

In the model: board 25 x 23.9 mm (x -12.5..12.5, y -16.88..6.98), board + parts z -2.38..0,
holder 22 x 14 mm z -7.58..-2.38, lens z -16.58..-7.58 (z negative towards the lens).

## Both orientations (2026-10-24)

The enclosure takes each camera as modelled and rotated 180 degrees about the lens axis (camera y -> -2.2 - y;
DESIGN.md "Camera orientation", bugs/bug-005). Rotated, the FFC connector sits at the antenna-end edge and the flex
runs under the camera and turns round that edge into it (S route): ~83 mm cable for the top camera (J4), ~70 mm for
the bottom one (J5), measured in fit.SLDASM with this model (10 cm cables for both). Re-check with the 222 degree
boards when they arrive.

## The stopgap camera is a different board (2026-10-24)

The Pi camera v1.3 clone from anodas.lt (bench tests only) does not match this model and is not what the enclosure
is for: board 24.6 x 23.92, holes ~21 x 12.7, the lens on the hole row nearest the FFC edge, a separate sensor
module with an 8.76 x 1.32 connector strip on the lens side, and a 22.3 wide, 2.5 high, 6 mm deep 15-pin FFC
connector apparently on the back (measured by the user). It is not put into the model or the enclosure.
