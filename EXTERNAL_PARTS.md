# External (off-board) parts

Parts that are not on the LEAKCAM PCB but are needed to build a unit: battery, battery
thermistor, camera modules and the camera cable. Ordered on AliExpress (order list, Sep 2026).
Listing ids expire; the "actual hardware" column is what the listing physically is, so the
part can be re-sourced from any vendor.

| # | Ordered (AliExpress) | Variant / qty | Actual hardware | Mates with on the board |
|---|---|---|---|---|
| 1 | [1005004518439613](https://www.aliexpress.com/item/1005004518439613.html) MF52D NTC, Shenzhen Weiheng Store | 10k B3435 L10CM, 5 pcs x1, EUR 0.42 | MF52D-103F-3435 epoxy bead NTC (Cantherm / Nanjing Shiheng MF52D 103F3435-100 equivalent), 3 mm head, 100 mm leads | JT (TS pad of the BQ24072 charger) to GND |
| 2 | [1005012836237910](https://www.aliexpress.com/item/1005012836237910.html) 503450/523450 LiPo, da da xiong Authorized Store | 2 pcs x1, EUR 5.69 | 503450 lithium-polymer cell, 3.7 V 1000 mAh, with protection PCM and JST PH 2.0 2-pin plug | S1, JST S2B-PH-K-S (bottom side) |
| 3 | [1005008549434548](https://www.aliexpress.com/item/1005008549434548.html) 15-to-22 pin FPC, HONG KONG CCD LIMITED | 4 cm, x1, EUR 1.49 | Raspberry Pi Zero style camera adapter FFC: 15-pin 1.0 mm pitch (camera end) to 22-pin 0.5 mm pitch (board end), 40 mm, gold contacts | J4 (bottom) and J5 (top), 05B20L22P 22-pin 0.5 mm |
| 4 | [1005003352074982](https://www.aliexpress.com/item/1005003352074982.html) OV5647 camera "for Raspberry Pi 3/4", Aideepen Office Store | 222 Degree, x2, EUR 8.89 each | Raspberry Pi camera v1.3 form-factor board (25 x 24 mm) with OV5647 5 MP sensor and an M12 222-degree fisheye lens, 15-pin FFC connector | J4 / J5 through part 3 |

## 1. Thermistor MF52D-103F-3435

- R25 = 10 kOhm +-1 %, B25/50 = 3435 K, operating range -40 to +125 C, dissipation
  constant 2 mW/C, thermal time constant about 7 s, max power 50 mW (MF52 series datasheet).
- The BQ24072 TS input is designed around a 10 k NTC with B = 3435 (Semitec 103AT curve);
  the resulting charge window is about 0 to 45 C. The B3950 variant sold in the same
  listing would shift both thresholds; do not substitute it.
- Wiring: one lead to JT (TS), the other to GND. Glue the bead to the battery pouch.
- Charger settings on the board for reference: R7 1 k on ISET (about 0.89 A fast charge),
  R5 1.5 k on ILIM (about 1.0 A input limit), R21 47 k on TMR (about 6.3 h safety timer).

## 2. Battery 503450, 1000 mAh

- Nominal 3.7 V, charge limit 4.20 V, discharge cut-off 3.0 V, standard charge 1 C
  (1000 mA), max continuous discharge 1 C, charge temperature 0 to 45 C, about
  5.0 x 34 x 50 mm, about 22 g (generic 503450 specification; "523450" in the title is
  the 5.2 mm thick sibling). Built-in PCM for over-charge / over-discharge / short.
- The board charges at about 0.89 A, which is 0.9 C; within the cell's 1 C rating.
- Connector: JST PH 2.0 mm 2-pin plug, mates with S1 (S2B-PH-K-S).
- **Check polarity before plugging in.** Aftermarket packs do not follow one convention for
  which PH pin is positive. Measure the plug against S1 pin 1 (BAT) / pin 2 (GND) first;
  the PCM does not protect against reverse connection.

## 3. Camera adapter cable, 15-pin to 22-pin, 40 mm

- Standard Raspberry Pi Zero camera cable: 15-pin 1.0 mm end into the camera board,
  22-pin 0.5 mm end into J4/J5 (05B20L22P, verified compatible with the Pi Zero camera
  connector pinout).
- **Only one cable was ordered for two cameras; a second one is needed.** The 15-pin FFC
  usually bundled with the camera board does not fit the 22-pin connector.
- Cable length sets where the folded-back camera lands. With the connectors at the lower
  board edge and the cable folded 180 degrees, a 40 mm cable puts the 25 x 24 mm camera
  board at about y = 35 to 59 mm, overlapping the antenna element at y = 55.5 to 60.7 mm.
  Keep the camera board's upper edge below y = 43 mm (12 mm from the antenna, about
  lambda/10 at 2.45 GHz): loop the slack in the fold or use a shorter cable. The bottom-side
  camera sits over the ground plane and is not critical, but keep it to the same limit.

## 4. Camera module OV5647, 222-degree fisheye

- Board: Raspberry Pi camera v1.3 clone, 25 x 24 mm, 15-pin FFC connector on the short
  edge, four mounting holes.
- Sensor: OmniVision OV5647, 1/4 inch, 5 MP (2592 x 1944 stills), 1080p30 / 720p60 video,
  2-lane MIPI CSI-2, I2C address 0x36.
- Lens: M12 fisheye marked 222 degrees. On a 1/4 inch sensor such lenses image a circle
  smaller than the sensor, so the usable picture is a circular field with a dark border and
  the effective resolution is below the sensor's. The listing's "3.6MM" is a separate
  variant name (a 3.6 mm standard lens, roughly 65 degrees), not a property of the
  222-degree unit.
- Two ordered: CAM1 on J5 (top side, MIPI_RX2 on the K230D) and CAM2 on J4 (bottom side,
  MIPI_RX0). Both are driven from 3V3 through J4/J5 pin 22.
- Mechanical: a 222-degree lens must see past the board edge and the enclosure; nothing
  may protrude into its hemisphere, and the two cameras on opposite sides give near-full
  spherical coverage only if both lenses stand proud of the enclosure surface.
