# test001: power and charging

Board: MIFA rev 1 (fab 11a6a38). Rails and limits: [tools/power-bringup](../../tools/power-bringup/README.md)
(`power_check.py`).

## Battery only, 2026-10-17

The cell on JB1 (3.9 V), USB unplugged. Wiring: [hookup_battery.png](../../tools/power-bringup/hookup_battery.png);
probe points: [battery_only.png](battery_only.png).

| # | Rail | Measured | Limit | |
|---|---|---|---|---|
| 2 | BAT | ~3.9 V | the cell | pass |
| 3 | VBAT | ~3.9 V | BAT - ~0.1 V | pass |
| 4 | 3V3_SLEEP | 3.298 V | 3.2 .. 3.4 V (3.31 nominal) | pass |

**Pass.** U5 (TPS63802) regulates from the battery; the X-ray had shown it slightly off its pads. U1 passes the
cell to VBAT.

## Charging from USB-C 1, 2026-10-17

The cell on JB1 (3.9 V), thermistor on JT, USB on USB-C 1. Probe points: [charging.png](charging.png). Charger
set to ~0.89 A (ISET R7 1 k), 1.0 A input limit (ILIM R5 1.5 k), 6.3 h timer (TMR R21 47 k); red D1 = /CHG.

| # | Rail | Measured | Limit | |
|---|---|---|---|---|
| 1 | VUSB | 5.2 V | 4.75 .. 5.25 V | pass |
| 2 | BAT | 3.9 V, not rising | above the cell, rising to 4.20 V | **fail** |
| 3 | VBAT | 4.2 V | BAT + ~0.2 V | U1 regulates OUT, not charging |
| | D1 | on | on while charging | on in a TS fault too |

**Fail: no charging**, [bug-001](../../bugs/bug-001/README.md). Retest after the 47 k workaround.

## Obstacles

- **No charging** -> U1 is BQ24072**T** (no internal TS bias), circuit drawn for the plain BQ24072 -> rev 1: 47 k
  VUSB-TS; rev 2: BQ24072RGTR.
- **Wrong NTC window** -> assumed B3950, part is B3435 -> take constants from EXTERNAL_PARTS.md.
- **Cell drained to 2.9 V after a day of bring-up** -> no charging and hours without USB power (bug-002 before it
  was understood) -> K230 rails off; charge the cell off the board (4.2 V, 100 mA below 3.0 V) or fit the 47 k.
  No K230 flashing at 2.9 V (brown-out during a NAND write). D1 went off: charger timer / pre-charge state.
