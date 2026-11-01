# Bring-up tools: MIFA rev 1

The tools for the first power-on of a new board: what to solder by hand, how to wire the debug
port and the rail checks with a multimeter (the BL616 test firmware: [tests/firmware](../../tests/firmware/README.md)). Test records, readings and
results: [tests/](../../tests/README.md).

**Order**

1. Rail-to-GND resistance, power off (`power_check.py off`). Nothing soldered yet.
2. USB on USB-C 1, BL616 still blank (`power_check.py blank`): only VBAT and 3V3_SLEEP may be up.
3. Wire the debug port PR1, flash the test firmware ([tests/firmware/bl616_powertest](../../tests/firmware/bl616_powertest)), switch
   the K230 rails on from its console and measure them (`power_check.py flashed`).
4. Solder the battery, thermistor, leak connectors and LED strips; check each with the test firmware.
5. Flash the product firmware ([firmware/BUILD.md](../../firmware/BUILD.md)).

## 1. Rail checks (multimeter)

```
python3 power_check.py all        # or: off | blank | flashed
```

The script names every pad to probe, checks the reading against the limits and appends it to
`power_check_<board>.csv`. The numbers in the pictures are the numbers it asks for. All probe
points are on the **top** side (the regulators are on the bottom, their output capacitors on top). Black lead on the black ring, red lead on the red ring.

| Full top side | Power corner (zoom) |
|---|---|
| ![top](../../tests/test001-power-and-charging/top.png) | ![zoom](../../tests/test001-power-and-charging/top_power_zoom.png) |

### Power tree

```
USB-C 1 (USBC1) -> U1 BQ24072 -> VBAT
VBAT -> U5 TPS63802, EN tied to VBAT (always on) ----------------> 3V3_SLEEP  (BL616 U11, PR1.6)
BL616 IO30 = K230_PWR (R19 100k pull-down: low while the BL616 is blank)
  -> R51 -> U6 TPS62823 EN -----------------------------------------> 0V8 (+ FB3 -> 0V8_MIPI)
            U6 power-good (R27 to K230_PWR) -> U8 TPS63802 ---------> 3V3
                                            -> U10 TPS63802 --------> 1V8 (+ FB1, FB2 -> 1V8_MIPI, 1V8_DDR)
            U10 power-good = PG_1V8 (R28 to K230_PWR) -> U7 TPS62823 -> 1V1
            U7 power-good = RSTN (R30 to 1V8, C23) -> K230 reset; the BL616 holds it low through Q4
```

### Limits

| # | Rail | + pad | GND pad | off: ohms to GND | blank BL616 | rails on | battery only |
|---|---|---|---|---|---|---|---|
| 1 | VUSB | C2.1 | C2.2 | >= 20 | 4.75 .. 5.25 V | 4.75 .. 5.25 V | <= 0.3 V |
| 2 | BAT | C11.1 | C11.2 | >= 20 | (no battery) | (no battery) | the cell, 3.0 .. 4.25 V |
| 3 | VBAT | C3.1 | C3.2 | >= 20 | 4.1 .. 4.6 V | 4.1 .. 4.6 V | BAT - ~0.1 V |
| 4 | 3V3_SLEEP | C14.1 | C14.2 | >= 20 | 3.2 .. 3.4 V | 3.2 .. 3.4 V | 3.2 .. 3.4 V |
| 5 | K230_PWR | R19.2 | R19.1 | | <= 0.3 V | 3.0 .. 3.4 V | <= 0.3 V |
| 6 | 0V8 | C18.1 | C18.2 | >= 2 | <= 0.1 V | 0.76 .. 0.84 V | <= 0.1 V |
| 7 | 3V3 | C87.1 | C87.2 | >= 20 | <= 0.1 V | 3.2 .. 3.4 V | <= 0.1 V |
| 8 | 1V8 | C89.1 | C89.2 | >= 10 | <= 0.1 V | 1.71 .. 1.85 V | <= 0.1 V |
| 9 | PG_1V8 | R28.1 | C89.2 | | | 3.0 .. 3.4 V | |
| 10 | 1V1 | C20.1 | C20.2 | >= 5 | <= 0.1 V | 1.05 .. 1.15 V | <= 0.1 V |
| 11 | RSTN | R30.2 | C23.2 | | | ~0 V (held) or ~1.8 V (released) |
| 12 | 0V8_MIPI | C41.1 | C41.2 | | | 0.76 .. 0.84 V |
| 13 | 1V8_MIPI | FB1.2 | C89.2 | | | 1.71 .. 1.85 V |
| 14 | 1V8_DDR | FB2.2 | C89.2 | | | 1.71 .. 1.85 V |

Nominal outputs from the feedback dividers: 3V3_SLEEP and 3V3 0.5 x (1 + 511k/91k) = 3.31 V,
1V8 0.5 x (1 + 51k/20k) = 1.78 V, 0V8 0.6 x (1 + 33k/100k) = 0.80 V, 1V1 0.6 x (1 + 100k/120k) =
1.10 V. VBAT is the BQ24072 output: about 4.4 V on USB without a battery, the battery voltage on
battery.

- **off**: unplug USB and the battery, measure each rail to GND in ohms. The reading climbs while
  the capacitors charge; take it after about 3 s. Below the minimum is a short: stop.
- **blank**: USB on USB-C 1, no battery, nothing on PR1. R19 holds K230_PWR low, so only **VBAT and
  3V3_SLEEP** are up. Any K230 rail above 0.1 V means a regulator runs without its enable: stop.
- **battery only** (`battery` in the script): USB unplugged, the cell on JB1. The charger passes the cell
  to VBAT, so **BAT, VBAT and 3V3_SLEEP** are up and the K230 rails off (unless the BL616 firmware has
  switched them on). VUSB reads ~0 V.
- **rails on** (`flashed` in the script): with the test firmware, `pt_on` drives K230_PWR high and
  the chain brings up **0V8, then 3V3 and 1V8, then 1V1**. The product firmware does the same on
  its first boot. RSTN is ~0 V while the K230 is held in reset and ~1.8 V once released.

A rail at the wrong voltage, or a regulator that gets hot: power off at once. The X-ray of the
first boards showed U5 (3V3_SLEEP) and U6/U7 (0V8/1V1) slightly off their pads; these checks are
what confirms or rules that out.

## 2. Debug port PR1 (bottom side)

![bottom side wiring](../../tests/test002-bl616-flash-and-console/hookup_bottom.png)

![PR1 debug cable, zoom](../../tests/test002-bl616-flash-and-console/hookup_debug.png)

PR1 is the BL616's own USB: no USB-serial bridge is needed, a USB cable is soldered to the pads.
Cut a USB-A cable, solder **green D+ to pad 1, white D- to pad 2, black GND to pad 3**. Cut the red
VBUS wire back and tape it: the board powers the BL616 itself (USB-C 1 or the battery); pad 6 is the
board's 3V3_SLEEP, an output.

A **USB-C cable** (for a Mac or any USB-C port) has a fifth wire, usually yellow: CC. A USB-C port stays off until it
sees 5.1 k from CC to GND, so fit **5.1 k (4.7 .. 5.6 k) between the yellow and the black wire**; D+/D-/GND as above,
red VBUS still cut and taped (the port turns on 5 V once CC is terminated). Without it the Mac shows nothing. A
USB-C to USB-A adapter or a hub on the computer side does this itself. On macOS the port is `/dev/cu.usbmodem*`.

- **Flash mode without wires** (normal case): while a firmware's USB console runs, `make flash` sends it
  `reboot uart` ([bl616_isp.py](bl616_isp.py)): the ROM starts as with BOOT high; after flashing, `bl616_isp.py --boot`
  clears the ROM's user-boot field (HBN_RSV2) and resets: the new firmware starts, no EN tap. Only a firmware that never reaches its console needs the BOOT link below.
- **Boot into flash mode by hand**: link pad 4 (BOOT) to pad 6 (3V3_SLEEP), then touch pad 5 (EN) to GND
  (pad 3) for a moment and let go. The ROM enumerates as `Bouffalo CDC` (349b:6160), `/dev/ttyACM0`.
- **Run**: remove the 4-6 link and touch pad 5 to GND again. The firmware's console appears on the
  same cable as `/dev/ttyACM0` (any baud rate): `picocom /dev/ttyACM0`.

## 3. BL616 test firmware

The test firmware and the K230 hello program are in [tests/firmware](../../tests/firmware/README.md).

## 4. Hand-soldered parts

![top side wiring](../../tests/test001-power-and-charging/hookup_top.png)

The bottom side (PR1 cable, leak probes, camera 2) is the picture in section 2.

- **Battery** (503450 LiPo, [EXTERNAL_PARTS.md](../../EXTERNAL_PARTS.md)): cut the plug off,
  **measure which lead is positive**, solder it to JB1 pin 1 (BAT), the other to pin 2 (GND). No
  reverse protection on the board. A second cell goes to JB2 the same way, voltages matched to
  within 0.1 V first.
- **Thermistor** (MF52D 10k B3435): JT pin 1 (TS) and pin 2 (GND), no polarity; glue the bead to
  the battery. Without it the charger sees a TS fault and does not charge (the board still runs on
  USB).
- **Leak connectors S1..S3** (JST PH 2-pin, through-hole, bottom side): no polarity. Check with
  `pt_leak`: dry ~1650 mV; a wet probe or a resistor across it pulls the node down.
- **Cameras**: 15-to-22 pin Pi Zero cable, 22-pin end into J5 (camera 1, top) and J4 (camera 2,
  bottom), contacts as on a Pi Zero.
- **B1_P / B1_N**: short while the K230 powers up to force its USB flash mode (BUILD.md 3.2).

### LED strips

The four strips (A..D, cut from the panel) carry 2 white + 2 IR LEDs each. **Each colour is one
series chain through all four strips**: 8 white LEDs on J1 (about 22 V from U14) and 8 IR LEDs on
J2 (about 11 V from U15). Twist each go/return pair.

![LED strip wiring](../../tests/test009-led-chains/led_wiring.png)

The wires, numbered in soldering order (made to print in black and white: white chain solid, IR dashed).

- IR pairs (J6, J8, J10, J12): **pin 2 = +**, pin 1 = - (measured on the rev 1 strips, 2026-10-16).
  The JNJ footprint does not number its pads like the datasheet: its anode pad (right) is "1", the
  cathode pad (left) "3", the centre "2". The IR chain as built runs J2 + > D > C > B > A > J2 -
  (the picture shows it so).
- The board has no +/- marks at the wire pads (only J3 .. J12, J1, J2) and no square pin 1.
- White pairs (J3, J7, J9, J11): pin 2 = + (Xinglight footprint: cathode pad "1" left, anode "2"
  right). **Confirm before wiring**: a bench supply at 6.5 V with a 2 mA limit across one strip's white pair lights both LEDs
  faintly in the right polarity only (IR pair: 3.5 V, 2 mA, check with a phone camera). A chain
  wired backwards sees the boost converter's open-circuit voltage in reverse.
- The LEDs are driven by the K230 (PWM), so the chains are tested with the K230 firmware
  (`leakcam_capture -b`), not from the BL616.

## Files

- `testpoints.json`: probe pads and limits per phase
- `power_check.py`: the interactive multimeter check, writes the CSV log
- `render_testpoints.py`: redraws the pictures into the tests that use them (no images under tools/): probe points
  and battery wiring (`top.png`, `top_power_zoom.png`, `hookup_top.png`, `hookup_battery.png`) in test001, the PR1
  wiring (`hookup_bottom.png`, `hookup_debug.png`) in test002, `led_wiring.png` in test009, and each test's probe
  sheets into its `tests/testNNN-*/` folder, from the ODB++ of the ordered board (git `11a6a38`);
  it refuses to draw if a probe pad is not on the net the table says
