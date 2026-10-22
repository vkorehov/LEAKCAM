# test001: power and charging

Status: **pass: battery rails; charging with the bug-001 47 k** (2026-10-17/20).

Board: MIFA rev 1 (fab 11a6a38). Rails and limits: [tools/power-bringup](../../tools/power-bringup/README.md)
(`power_check.py`).

## Setup

Not written at the time; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

Board only: BQ24072 (hardware), no firmware; BL616 test firmware optional (pt_usb).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

Not written at the time; the steps are the rows of the Results tables.

## Pass

Not written at the time; every Results row carries its pass / fail.

## Results

### Battery only, 2026-10-17

The cell on JB1 (3.9 V), USB unplugged. Wiring: [hookup_battery.png](../../tools/power-bringup/hookup_battery.png);
probe points: [battery_only.png](battery_only.png).

| # | Rail | Measured | Limit | |
|---|---|---|---|---|
| 2 | BAT | ~3.9 V | the cell | pass |
| 3 | VBAT | ~3.9 V | BAT - ~0.1 V | pass |
| 4 | 3V3_SLEEP | 3.298 V | 3.2 .. 3.4 V (3.31 nominal) | pass |

**Pass.** U5 (TPS63802) regulates from the battery; the X-ray had shown it slightly off its pads. U1 passes the
cell to VBAT.

### Charging from USB-C 1, 2026-10-17

The cell on JB1 (3.9 V), thermistor on JT, USB on USB-C 1. Probe points: [charging.png](charging.png). Charger
set to ~0.89 A (ISET R7 1 k), 1.0 A input limit (ILIM R5 1.5 k), 6.3 h timer (TMR R21 47 k); red D1 = /CHG.

| # | Rail | Measured | Limit | |
|---|---|---|---|---|
| 1 | VUSB | 5.2 V | 4.75 .. 5.25 V | pass |
| 2 | BAT | 3.9 V, not rising | above the cell, rising to 4.20 V | **fail** |
| 3 | VBAT | 4.2 V | BAT + ~0.2 V | U1 regulates OUT, not charging |
| | D1 | on | on while charging | on in a TS fault too |

**Fail: no charging**, [bug-001](../../bugs/bug-001/README.md). Retest after the 47 k workaround.

### Charging with the 47 k workaround, 2026-10-19

47 k fitted from C2 pad 1 (VUSB) to JT pin 1 (TS) ([workaround_47k.png](../../bugs/bug-001/workaround_47k.png)).
USB on USB-C 1, K230 off.

| Step | Result | |
|---|---|---|
| TS (JT 1 to JT 2) | 0.88 V: inside the 0.65-1.3 V charge window (~26 C) | pass |
| First try, cell at 2.9 V | D1 flashes and goes off, BAT jumps: the charger's battery detection, no cell seen on BAT. Then read as a deep-discharged cell; from 2026-10-21 more likely the JB1 joint already failing (2.9 V is not deep: the cell's PCM had not cut it) | |
| Cell lifted on a bench supply, then board charger | bench run: 2.2 A falling to 1.6 A at 4.3 V (too much: use 4.20 V, <= 0.5 A); rest voltage after it 3.54-3.56 V | |
| Board charger, cell at ~3.56 V rest | BAT 3.64 V, then 3.71 V: charging | **pass** |
| Charge current | ~0.2-0.3 A estimated from BAT minus the rest voltage (0.08 V over an assumed ~0.3-0.45 ohm) and put down to U1's thermal regulation. Both wrong (2026-10-21): there is no such drop between the cell and BAT while charging, and with the cell wired well U1 gives the full 0.89 A | not measured |
| End of charge (2026-10-20) | D1 off next morning; cell 4.15 V at rest (overnight, not watched: no charge time) | **pass** |
| USB replugged at 4.15 V | D1 flashes once, then off: the cell is above the ~4.10 V recharge threshold, so the charger reports done at once. The overnight charge therefore ended by termination, not on the 6.3 h timer | pass |

**Pass: the board charges to full** with the 47 k (D1 off, 4.15 V at rest). Current and charge time: see 2026-10-21.

### Charging with the cell wired to C11, 2026-10-21

The cell's + wired straight to C11 pad 1 (BAT), the JB1 joint bypassed (see Obstacles). USB meter in VBUS, K230 off.

| Step | Result | |
|---|---|---|
| Start, cell ~3.6 V | 0.94 A on the USB meter = 0.89 A into the cell + 51 mA BL616/board | **pass**: full set current, U1 not throttling |
| After ~1 h | 350 mA for a while; a replug started at 0.45 A and ramped down quickly to 350 mA | not explained |
| Later, same charge | 0.94 A again (0.89 A + 51 mA) | so the 350 mA was not the CV taper; U1 not throttling |
| Then | 290 mA, then 250 mA, falling (~0.20 A into the cell) | CV taper at 4.20 V |
| BAT (C11 pad 1) at ~250 mA, 19:30 | 4.18 V (VBAT 4.37 V) | CV phase confirmed: the taper is the cell, not U1's heat |
| End of charge | D1 off, reported 20:13 (exact time not watched; the cell ran the K230 on battery 19:31-19:41 in between, test023) | **pass**: terminates |

**What the earlier charger problems were.** Every one from 2026-10-19 to 10-21 fits the JB1 cell joint, not U1 or
the cell: D1 flashing / "no cell" and BAT jumping
(joint open), the board resetting on every VBUS interruption (no cell behind VUSB), and C11 at 3.30 V while the
cell read 3.69 V at its wires. The low "~0.2-0.3 A" was never measured (a wrong voltage-drop estimate) and the
"~6 h" was an unwatched overnight charge. There is no evidence that the cell was ever deep-discharged or that its PCM tripped;
the "3.0 V cut-off" is only the generic cell spec's recommended end of discharge (EXTERNAL_PARTS.md), not a
protection in the pack. U1's heat: not seen; it holds 0.89 A.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (to fill on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, VBAT C15.1, BAT C11.1, 3V3_SLEEP (charging.png / battery_only.png).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | |
| Test run time | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | not measured: run before the metrics rule (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | not measured: run before the metrics rule (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | not measured: run before the metrics rule (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: rails-on time (3V3 up 1.7 ms after K230_PWR, measured in test003's pt_on) and RSTN delay: the floor of every boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: the battery-only rails' quiescent current (BL616 running the test firmware, K230 rails off and on); charging is from USB, n/a.

## Obstacles

- **No charging** -> U1 is BQ24072**T** (no internal TS bias), circuit drawn for the plain BQ24072 -> rev 1: 47 k
  VUSB-TS; rev 2: BQ24072RGTR.
- **Wrong NTC window** -> assumed B3950, part is B3435 -> take constants from EXTERNAL_PARTS.md.
- **Cell at 2.9 V after a day of bring-up** -> no charging (bug-001) and hours without USB power (bug-002 before it
  was understood) -> K230 rails off; fit the 47 k. Not deep: the cell's PCM did not cut it at 2.9 V.
- **D1 flashes and goes off, BAT jumps (with the 47 k)** -> the charger sees no cell (battery detection cycling);
  then blamed on a deep discharge, most likely the JB1 joint already failing -> lifted the cell on a bench supply
  (4.20 V, <= 0.5 A; 4.3 V / 2.2 A was too much), then the board charger took over.
- **Charge current "looked low" (~0.2-0.3 A, 2026-10-19)** -> estimated from a cell-BAT drop that does not exist
  and blamed on U1's heat; measured on 2026-10-21 it is the full 0.89 A -> measure, don't guess: the USB meter, or
  V(R7) / 2.5 (ISET monitor, SLUS810 eq. 3).
  No K230 flashing at 2.9 V (brown-out during a NAND write).
- **No charging, D1 flashing 2 Hz, then one blink and off (2026-10-21)** -> the cell's + wire joint on JB1 pin 1 was
  cracked: the cell read 3.69 V at its wires, C11 (BAT) millivolts; first the pre-charge timer latched a fault (2 Hz,
  cleared only by removing the input), then the charger found no battery -> reflowed the joint; charges from a wall
  adapter. Probe sheets: charger_pins_top/bottom.png. The same open joint is why every VBUS interruption that day
  reset the board (no cell behind VUSB). The cell itself was fine (3.69 V).
- **D1 blinking, no charge, only with the ammeter in VBUS** (2026-10-21) -> the meter path (10 A shunt, leads, splice:
  a few tenths of an ohm) drops VUSB at the ~1 A charge start, and in ILIM mode (EN2 high) the charger has no
  VIN-DPM to back off, so it drops out and restarts -> charge with the meter bypassed (Mac port direct or a wall
  adapter); measure the board's current with a full cell (D1 off). bugs/v2.txt 9.
- **The reflowed JB1 joint opened again within the hour** (2026-10-21; D1 went off, 71 mA, cell disconnected) ->
  the cell wire loads the joint mechanically -> wire through the plated hole, solder wetting both sides with flux
  and a hot tip, strain relief (glue or tape 5-10 mm from the pad); pull test, then C11 = the cell with USB out.
- **Rework (2026-10-21):** the cell's + now goes by copper wire straight to C11 pad 1 (BAT), bypassing the JB1 joint.
  The BAT copper itself is fine (0.51 mm tracks + pour on top, good for ~1.5-2 A; the board charges at <= 1 A);
  the JB1 failures were mechanical. Fix the wire down near C11 so it does not load the small pad.
