# LED ring boards (one per camera side)

Illumination for the two fisheye cameras: a small ring PCB on each camera, 4 white + 4 IR 850 nm
LEDs interleaved at 45 degrees, all 8 in one series string driven by the main board's TPS61160
(U14 -> J1, U15 -> J2). The IR LEDs are only useful with the cameras' IR-cut filter removed.

## Parts per ring, and for both rings

| Ref (per ring) | Part | LCSC | Package | Per ring | Both rings | Order (+20 % spare) |
|---|---|---|---|---|---|---|
| D1, D3, D5, D7 (0, 90, 180, 270 deg) | White LED, Everlight 67-21S/KK7C, 6500 K, 2.8-3.4 V, 150 mA max | [C385305](https://www.lcsc.com/product-detail/C385305.html) | SMD2835-2P | 4 | 8 | 10 |
| D2, D4, D6, D8 (45, 135, 225, 315 deg) | IR LED 850 nm, Chongtian CT-2835IR850-PT, 1.5-1.8 V, 100 mA max, 120 deg | [C53191414](https://www.lcsc.com/product-detail/C53191414.html) | SMD2835 | 4 | 8 | 10 |
| LED+, LED- | 2 solder pads for the lead to J1 or J2 | - | - | 2 | 4 | - |

Both LEDs are 2835, so one footprint serves both. The two makers mark polarity differently: check
each datasheet's cathode mark before drawing the footprint.

## Electrical (set on the main board)

- String: D1 -> D2 -> ... -> D8 in ring order, anode end to LED+ (J1.1 / J2.1, the boost output),
  cathode end to LED- (J1.2 / J2.2, the FB sense resistor). Order along the ring does not matter,
  every LED carries the same current.
- R66 (J1) and R68 (J2) are **5.6 ohm** ([C322191](https://www.lcsc.com/product-detail/C322191.html),
  0603, same FP-SR0603-MFG footprint as before): I = 0.2 V / 5.6 ohm = **35.7 mA** at full duty.
- String voltage 18.2 V typical, 21.0 V worst case: under the TPS61160's 37 V minimum OVP, the
  40 V DSK24 diodes and the 50 V output capacitors (C1, C104).
- Driver switch peak at a 3.0 V battery: 0.48 A, under the 0.56 A minimum current limit.
- Brightness by PWM on CTRL (GPIO60 = PWM0, GPIO61 = PWM1, 25 kHz): the TPS61160 turns duty into
  a DC current, so no banding on the rolling-shutter OV5647. White and IR always light together.
- Battery: both rings on plus the K230 is over 1 A. Light one side at a time where possible.

## Mechanical

- About 30 mm across, 2-layer, 1.0 mm or thinner.
- Centre hole ~17 mm for the M12 lens barrel (check against the lens you received).
- 4 x M2 holes on the camera board's hole pattern, mounted on standoffs over the camera PCB.
- **Keep every LED out of the lens' view.** A 222-degree lens sees 21 degrees behind its own front
  plane. Mount the ring at the base of the lens holder, well behind the front element (about 15 mm
  back and 12 mm off-axis puts each LED ~51 degrees below the lens plane, outside that cone), or add
  a short shroud. An LED the lens can see flares into the picture.
