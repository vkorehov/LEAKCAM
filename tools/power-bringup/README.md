# Power bring-up: MIFA rev 1

First power-on of a new board, with a multimeter, in three phases. Each phase only starts when
the one before passed: a wrong rail can kill the K230D.

```
python3 power_check.py all        # or: off | blank | flashed
```

The script names every pad to probe, checks the reading against the limits and appends it to
`power_check_<board>.csv`. The numbers in the pictures are the numbers it asks for.

| Full top side | Power corner (zoom) |
|---|---|
| ![top](top.png) | ![zoom](top_power_zoom.png) |

All probe points are on the **top** side: the regulators are on the bottom
([bottom.png](bottom.png), seen from below), their output capacitors on top. Black lead on the
black ring (the GND pad of the same capacitor), red lead on the red ring.

## Power tree

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

## Phases and limits

| # | Rail | + pad | GND pad | off: ohms to GND | blank BL616 | flashed BL616 |
|---|---|---|---|---|---|---|
| 1 | VUSB | C2.1 | C2.2 | >= 20 | 4.75 .. 5.25 V | 4.75 .. 5.25 V |
| 2 | BAT | C11.1 | C11.2 | >= 20 | (no battery) | (no battery) |
| 3 | VBAT | C3.1 | C3.2 | >= 20 | 4.1 .. 4.6 V | 4.1 .. 4.6 V |
| 4 | 3V3_SLEEP | C14.1 | C14.2 | >= 20 | 3.2 .. 3.4 V | 3.2 .. 3.4 V |
| 5 | K230_PWR | R19.2 | R19.1 | | <= 0.3 V | 3.0 .. 3.4 V |
| 6 | 0V8 | C18.1 | C18.2 | >= 2 | <= 0.1 V | 0.76 .. 0.84 V |
| 7 | 3V3 | C87.1 | C87.2 | >= 20 | <= 0.1 V | 3.2 .. 3.4 V |
| 8 | 1V8 | C89.1 | C89.2 | >= 10 | <= 0.1 V | 1.71 .. 1.85 V |
| 9 | PG_1V8 | R28.1 | C89.2 | | | 3.0 .. 3.4 V |
| 10 | 1V1 | C20.1 | C20.2 | >= 5 | <= 0.1 V | 1.05 .. 1.15 V |
| 11 | RSTN | R30.2 | C23.2 | | | ~0 V (held) or ~1.8 V (released) |
| 12 | 0V8_MIPI | C41.1 | C41.2 | | | 0.76 .. 0.84 V |
| 13 | 1V8_MIPI | FB1.2 | C89.2 | | | 1.71 .. 1.85 V |
| 14 | 1V8_DDR | FB2.2 | C89.2 | | | 1.71 .. 1.85 V |

Nominal outputs from the feedback dividers: 3V3_SLEEP and 3V3 0.5 x (1 + 511k/91k) = 3.31 V,
1V8 0.5 x (1 + 51k/20k) = 1.78 V, 0V8 0.6 x (1 + 33k/100k) = 0.80 V, 1V1 0.6 x (1 + 100k/120k) =
1.10 V. VBAT is the BQ24072 output: about 4.4 V on USB without a battery, the battery voltage on
battery.

1. **off**: unplug USB and the battery, measure each rail to GND in ohms. The reading climbs while
   the capacitors charge; take it after about 3 s. Below the minimum is a short: stop.
2. **blank**: USB on USB-C 1, no battery, nothing on PR1. The BL616 is blank, R19 holds K230_PWR
   low, so only **VBAT and 3V3_SLEEP** are up. Any K230 rail above 0.1 V means a regulator runs
   without its enable: stop.
3. **flashed**: flash the BL616 through PR1 ([firmware/BUILD.md](../../firmware/BUILD.md) section
   4.3), release BOOT, pulse EN. Its first boot drives K230_PWR high and the chain brings up
   **0V8, then 3V3 and 1V8, then 1V1**: all five regulators. Measure in the first minutes (the
   debug build keeps the rails on while it waits for the K230). RSTN is ~0 V while the BL616
   still holds the K230 in reset and ~1.8 V after it lets go; both are correct.

A rail at the wrong voltage, or a regulator that gets hot: power off at once. The X-ray of the
first boards showed U5 (3V3_SLEEP) and U6/U7 (0V8/1V1) slightly off their pads; these phases are
what confirms or rules that out.

## Files

- `testpoints.json`: probe pads, notes and limits per phase (one place for both scripts)
- `power_check.py`: the interactive check, writes the CSV log
- `render_testpoints.py`: redraws `top.png`, `top_power_zoom.png`, `bottom.png` from the ODB++ of
  the ordered board (git `11a6a38`), and refuses to draw if a pad is not on the net the table says
