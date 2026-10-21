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

## Charging with the 47 k workaround, 2026-10-19

47 k fitted from C2 pad 1 (VUSB) to JT pin 1 (TS) ([workaround_47k.png](../../bugs/bug-001/workaround_47k.png)).
USB on USB-C 1, K230 off.

| Step | Result | |
|---|---|---|
| TS (JT 1 to JT 2) | 0.88 V: inside the 0.65-1.3 V charge window (~26 C) | pass |
| First try, cell at 2.9 V | D1 flashes and goes off, BAT jumps: the charger's battery detection, no cell seen on BAT (deep-discharged cell) | |
| Cell lifted on a bench supply, then board charger | bench run: 2.2 A falling to 1.6 A at 4.3 V (too much: use 4.20 V, <= 0.5 A); rest voltage after it 3.54-3.56 V | |
| Board charger, cell at ~3.56 V rest | BAT 3.64 V, then 3.71 V: charging | **pass** |
| Charge current | ~0.2-0.3 A estimated (BAT - rest = 0.08 V over ~0.3-0.45 ohm), below the 0.89 A set point: likely U1's thermal regulation, ~1.4 W at (5.2 - 3.6) V x 0.89 A | note |
| End of charge (2026-10-20) | D1 off; cell 4.15 V at rest; about 6 h from ~3.56 V (not watched in between) | **pass** |
| USB replugged at 4.15 V | D1 flashes once, then off: the cell is above the ~4.10 V recharge threshold, so the charger reports done at once. The overnight charge therefore ended by termination, not on the 6.3 h timer | pass |

**Pass: the board charges to full** with the 47 k (D1 off, 4.15 V at rest). Note: ~6 h is close to the 6.3 h
safety timer (TMR R21 47 k), which also turns D1 off when it expires; 4.15 V at rest says the cell was full, but a cell
starting near empty may hit the timer at this low current. Not measured yet: the real current, V(R7) to GND / 2.5 =
charge current in A (ISET monitor, V_ISET = I_charge / 400 x R_ISET, datasheet SLUS810 eq. 3), no wire cut needed.



## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: rails-on time (3V3 up 1.7 ms after K230_PWR, measured in test003's pt_on) and RSTN delay: the floor of every boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: the battery-only rails' quiescent current (BL616 running the test firmware, K230 rails off and on); charging is from USB, n/a.

## Obstacles

- **No charging** -> U1 is BQ24072**T** (no internal TS bias), circuit drawn for the plain BQ24072 -> rev 1: 47 k
  VUSB-TS; rev 2: BQ24072RGTR.
- **Wrong NTC window** -> assumed B3950, part is B3435 -> take constants from EXTERNAL_PARTS.md.
- **Cell drained to 2.9 V after a day of bring-up** -> no charging and hours without USB power (bug-002 before it
  was understood) -> K230 rails off; charge the cell off the board (4.2 V, 100 mA below 3.0 V) or fit the 47 k.
- **D1 flashes and goes off, BAT jumps (with the 47 k)** -> the charger sees no cell (battery detection cycling) after
  the deep discharge -> lift the cell on a bench supply first (4.20 V, <= 0.5 A; 4.3 V / 2.2 A was too much), then
  the board charger takes over.
- **Charge current looks low** -> a linear charger at a low cell voltage throttles for heat -> read V(R7) / 2.5 instead
  of guessing from voltages.
  No K230 flashing at 2.9 V (brown-out during a NAND write). D1 went off: charger timer / pre-charge state.
- **No charging, D1 flashing 2 Hz, then one blink and off (2026-10-21)** -> the cell's + wire joint on JB1 pin 1 was
  cracked: the cell read 3.69 V at its wires, C11 (BAT) millivolts; first the pre-charge timer latched a fault (2 Hz,
  cleared only by removing the input), then the charger found no battery -> reflowed the joint; charges from a wall
  adapter. Probe sheets: charger_pins_top/bottom.png. The same open joint is why every VBUS interruption that day
  reset the board (no cell behind VUSB), and the cell had been drained before it opened.
- **D1 blinking, no charge, only with the ammeter in VBUS** (2026-10-21) -> the meter path (10 A shunt, leads, splice:
  a few tenths of an ohm) drops VUSB at the ~1 A charge start, and in ILIM mode (EN2 high) the charger has no
  VIN-DPM to back off, so it drops out and restarts -> charge with the meter bypassed (Mac port direct or a wall
  adapter); measure the board's current with a full cell (D1 off). bugs/v2.txt 9.

