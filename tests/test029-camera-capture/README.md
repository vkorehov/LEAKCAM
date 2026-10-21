# test029: cameras: detection, capture, image

Status: **placeholder** (2026-10-21): the cameras are not delivered yet (two OV5647 with 222 degree M12 fisheye
lenses, EXTERNAL_PARTS.md; Arducam B0370 format on J4/CSI0 and J5/CSI2).

## Steps (to detail when they arrive)

1. Detection: each sensor answers on its I2C bus (IIC0 for CAM2/J4, IIC4 for CAM1/J5), chip ID 0x5647.
2. Capture: both at 1280x960 binned (DESIGN.md), single frames and continuous, VICAP offline mode; frames saved to
   /sdcard and checked on the PC.
3. Image: focus of the fisheye lens in the holder (lens raise per the enclosure), exposure in daylight and dark,
   white LEDs and IR LEDs on (test008 drivers), IR-cut behaviour, CAM_IO1 LED off (test028).
4. The full field of view: both cameras together cover the sphere (memory: 360 view is a hard requirement).
5. In the enclosure: lens openings clear, LED light not leaking into the lenses.

Speed (frame rate, I2C clock) is test019; the encoder with real frames test026; the whole leak path test103.


## Boot time

Sensor init to first frame, both cameras (adds to every session that captures).

## Battery energy

Battery current per camera capturing, with white and IR LEDs on; energy per capture of both cameras.
