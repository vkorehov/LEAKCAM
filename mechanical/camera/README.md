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
