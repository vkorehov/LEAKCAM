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

## The 222 degree camera, measured (2026-11-11, the user, calipers)

The real board arrived; the model above is the B0370's. **OV5647_222.SLDPRT** is the camera now: built as native features from these numbers, then made precise by hand in SolidWorks (the user, 2026-11-11: the components on the lens side, 1.2 mm over the board from the connector to the far edge; the holder standing on them, 8.4 mm over the board; the connector 5.4 deep). The part is the master: change it in SolidWorks, not by a script.

| What | Measured | B0370 model |
|---|---|---|
| Board | 25.03 (the FFC edge; 25.1 first) x 24.3 mm | 25.0 x 23.9 |
| Mounting holes, centre to centre | 20.8 (along the FFC edge) x 13.5 mm | 21 x 12.5 |
| Hole diameter | 2.25 mm (M2) | |
| Board thickness | ~1.15 mm | |
| Board corner radius | ~0.3 mm | |
| Lens holder (M12) | body 13.8 x 13.8 mm, two screw ears: 22.45 mm over the ears; each ear ~4.3 out from the body x ~4.53 wide (13.8 + 2 x 4.3 = 22.4); the ears point along the 25.03 edge (the FFC edge); the square part ~3.5 mm high above the board | 22 x 14, 5.2 high |
| Lens holder, round collar | on the square part: outer 14.45 mm, bore 11.3 mm (M12 x 0.5), ~5.15 mm high; holder ~8.4 mm high in all over the board (3.5 + 5.15 = 8.65: one of them approximate) | |
| Back (away from the lens) | flat but for the holder's two screw heads: 3.2 mm across, ~0.8 mm proud | |
| Hole row away from the FFC edge | centres 2.24 mm from that edge and 2.24 mm from the side edge | ~2 mm |
| Hole row nearer the FFC edge | 13.5 mm from the first row: 8.56 mm from the FFC edge | |
| Holes along the row | 2.24 and 23.04 mm from one side edge (1.99 mm from the other) | centred |
| FFC connector | 22.15 mm wide (22.12 first), centred (~1.44 mm from each side edge), 6 mm deep, 2.6 mm high, its outer face 0.3 mm in from the board edge | |

The FFC connector is on the 25.03 mm edge (the hole rows run along it), on the lens side, at the edge away from the lens (as in the B0370 model).

The 222 degree lens (M12_fisheye_222.SLDPRT, build_lens.py): threaded part 11.8 mm across, ~8.4 mm long; the front
with the glass 16 mm across (build_lens.py estimated BODY_D 15, GLASS_D 11). The front: a 16 mm
cylinder ~3 mm high, then the glass as an elliptic dome ~2 mm over it: the front ~5 mm in all from where the 16 mm
starts.

## Handling rules (2026-11-11, the first 222 degree unit failed)

- Plug or unplug a camera only with the K230 off (BL616 test firmware `pt_off`, product `k230 off`): the first unit
  sent frames until it was put back in with the K230 running, never after (I2C still answered). Close the connector
  latch gently: J5's plastic broke under repeated swaps.
- Focus by the sharpness number (tests/test030-camera-capture/shot.py), a quarter turn at a time.
