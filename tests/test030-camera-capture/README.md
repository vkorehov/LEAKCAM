# test030: cameras: detection, capture, image

Status: **in progress** (2026-11-11 22:24 UTC+2): both connectors capture with two standard-lens OV5647 (the 222
degree unit is faulty, the second not arrived): colour stills, real H.265 from both cameras at 15 / 30 fps decoded on
the PC; top 32.7 fps per camera with both. Open: the green colour cast (ISP calibration), focus, the 222 degree units.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

As test020 + capture pipeline (VICAP buffers, MMZ pools), leakcam_wake capture.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (to detail when they arrive)

1. Detection: each sensor answers on its I2C bus (IIC0 for CAM2/J4, IIC4 for CAM1/J5), chip ID 0x5647.
2. Capture: both at 1280x960 binned (DESIGN.md), single frames and continuous, VICAP offline mode; frames saved to
   /sdcard and checked on the PC.
3. Image: focus of the fisheye lens in the holder (lens raise per the enclosure), exposure in daylight and dark,
   white LEDs and IR LEDs on (test009 drivers), IR-cut behaviour, CAM_IO1 LED off (test029).
4. The full field of view: both cameras together cover the sphere (memory: 360 view is a hard requirement).
5. In the enclosure: lens openings clear, LED light not leaking into the lenses.

Speed (frame rate, I2C clock) is test020; the encoder with real frames test027; the whole leak path test103.

## Pass

**MISSING**.

## Result

2026-11-11, step 1 on J4 (after the bug-005 rework to top-contact connectors), K230 on USB power with the camera
plugged: 3V3 up (msh answers); `i2cdetect i2c0` -> 0x36 only, `i2cdetect i2c4` empty (J5 has no camera);
`i2c_transfer i2c0 w2@0x36 0x30 0x0a r2` -> 0x56 0x47 (chip ID OV5647). J5 and steps 2-5: to do.

2026-11-11, step 2 on J4, first picture (`shot.py 0 20`: `/sdcard/app/cam_shot` on the test image, BL616
bl616_powertest + `pt_on`/`pt_run`): 1280x960 binned, open 0.07 s, 20 settle frames + 1 in 1.09 s (AE settled), luma
min 0 / mean 34 / max 255 (a dim room), 206 KB compressed over the console in 4.4 s; cam0.png. The CSI0 path works.
The image is far out of focus (lights as large discs; the fisheye's image circle faint at the edges): the lens is not
at its focus in the holder yet. Lens fully in: sharpness (variance of the Laplacian) 2.4; unscrewed 1.7, 1.4; a hand
a few cm in front never showed, at any depth: the 222 degree lens forms no image on this board.

2026-11-11, the other OV5647 board (the stopgap, standard lens) on the same J4 and cable: chip ID 0x5647, a real
picture of the room (wall, switch box, cloth, a sleeve), sharpness 4.1 (centre 6.5). **Board, J4, cable, CSI0 and the
ISP work; the 222 degree camera's lens is the fault** (damaged, a missing/covered element, or not matched to its
holder): to inspect (look through it alone, films on the dome/rear glass) or swap lenses.

2026-11-11 later: after the lens was turned and the cameras swapped several times, the 222 degree camera answers on
I2C (0x5647) but delivers **no frames** ("no frame within 1000 ms"), on the cable and J4 that give pictures with the
stopgap camera (sharpness 5.4, centre 10.4), after reseating at both ends and clean power cycles (`pt_off`/`pt_on`).
Software ruled out: its registers read back exactly the driver's 1280x960p45 table (0x3034-0x3036 1a/21/6a, 0x4800 24,
0x3016/17 08/e0; 0x3018 ff and 0x0100 00 = the driver's own stream-off writes), and the same image streamed from it
earlier. (Reopened for a while when J5's plastic broke with the same symptom; settled below: the unit is faulty.) Its optics (no
image at any lens depth, before any swap) stay suspect. Camera tests go on with the stopgap camera.

2026-11-11, J5 (CSI2, IIC4) with the stopgap camera: first plug silent (no ACK on i2c4: the cable not seated / its
contact side), after reseating with the K230 off: 0x36 on i2c4 three of three, chip ID 0x5647, `shot.py 1 20` a real
picture (fabric close to the lens; open 0.08 s, 20 + 1 frames in 1.10 s). **Step 1 and the first frame pass on both
connectors.** Rule from the day: swap cameras only with the K230 off (`pt_off`), reseat square, latch closed.

2026-11-11 21:37-21:41, after the user resoldered J5 (its connector plastic had broken): J5 five pictures in a row,
J4 three; then a stopgap-type camera on each, `shot.py 01 20`: **both cameras at once**, 20 + 1 frames each in
1.62 s (offline mode), both real pictures (both.png). Open: the first dual run after a boot gave J5 a black frame (mean
0; next run fine); a dual run after three single runs failed "video buffer pools failed (MMZ too small?)" until a
reboot. Repeated 22:43 on the new image: 8 runs in one boot, single and dual mixed (1, 0, 01, 0, 1, 01, 1, 0), every
picture came (10 of 10), K230 heap available 7306688 B after each: no leak, the failure did not come back (MMZ use
itself is not visible in `free`; the old image's run stays unexplained).

2026-11-11 21:47-21:49, **the 222 degree camera is faulty (verdict)**: on J4 (pictures with the stopgap camera at
21:40) and on the resoldered J5 (five pictures in a row at 21:37) it answers on I2C (0x5647) but delivers no frame,
after clean power cycles, while the other camera in the same boot gives pictures. Its optics never formed an image
either (before any swap). Seller claim; the second 222 degree unit to test on arrival (lens: turn only to the first
resistance).

2026-11-11 22:22-22:24, the colour image (cam_shot keeps the NV12 chroma; shot.py converts BT.601) and the H.265 file
mode (`leakcam_stream -f`, fdump to the PC, [stream.py](stream.py)), board white LEDs 100 % (`shot.py 01 30 100`):
- **Colour from both cameras** (cam0.png, cam1.png): real colours, but a strong **green cast** (lit pixels R 125 /
  G 165 / B 132 on cam0) with AWB on (vicap_cap.c awb_enable): the ISP's colour calibration for this sensor + lens, to
  tune. Sharpness 9.4 (J4) / 1.4 (J5, an object close to the lens).
- **Real H.265 from both cameras at once**, 10 s, the product settings (CBR 500 kbit/s, 15 fps): cam0 146 frames
  14.5 fps 502 kbit/s, cam1 146 frames 14.5 fps 496 kbit/s, 1 IDR each; ffprobe: hevc Main 1280x960, 148 frames each,
  decodes (cam0_h265.png, cam1_h265.png). Pulled over the console at ~990 kbit/s (5 s per 620 KB).
- **Speed, both cameras at once**: `-r 30` -> 29.0 / 28.8 fps (holds); `-r 45` -> 32.7 / 32.7 fps: the top with two
  cameras (one ISP for both in offline mode, ~65 frames/s through it; one camera alone not measured). The product's
  15 fps has 2x margin.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| capture fps, resolution | **MISSING** | 1280x960 binned, 45 fps | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Sensor init to first frame, both cameras (adds to every session that captures).

## Battery energy

Current (USB-C 1 meter) per camera capturing, with white and IR LEDs on; energy per capture of both cameras.
