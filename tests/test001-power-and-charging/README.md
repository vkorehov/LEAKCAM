# test001: power and charging

Status: **pass** (2026-10-25): battery rails, USB detect and charging (full 0.89 A, CV taper, termination) with the
bug-001 47 k.

Board: MIFA rev 1 (fab 11a6a38). Rails and limits: [tools/power-bringup](../../tools/power-bringup/README.md)
(`power_check.py`).

## Setup

- Battery only: the cell (3.9 V), USB unplugged. Wiring: [hookup_battery.png](hookup_battery.png);
  probe points: [battery_only.png](battery_only.png).
- Charging: thermistor on JT, USB on USB-C 1. Probe points: [charging.png](charging.png). Charger set to ~0.89 A
  (ISET R7 1 k), 1.0 A input limit (ILIM R5 1.5 k), 6.3 h timer (TMR R21 47 k); red D1 = /CHG. 47 k fitted from C2
  pad 1 (VUSB) to JT pin 1 (TS) ([workaround_47k.png](../../bugs/bug-001/workaround_47k.png)).
- The cell's + goes by copper wire straight to C11 pad 1 (BAT), bypassing the JB1 joint (rework 2026-10-21).
- USB meter in VBUS, K230 off.

## Needs (build features and dependencies)

Board only: BQ24072 (hardware), no firmware; BL616 test firmware optional (pt_usb).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Battery only: BAT, VBAT, 3V3_SLEEP against their limits.
2. Charging from USB-C 1: VUSB, TS (JT 1 to JT 2), BAT rising, charge current on the USB meter, CV taper, end of
   charge (D1 off), replug at full.
3. Automated (run.py): USB detect (PGOOD) and K230 boot on the charger rail, 8 cycles.

## Pass

Rails within their limits; the cell charges at the set current and terminates (D1 off).

## Result

- Battery only (2026-10-17): BAT ~3.9 V, VBAT ~3.9 V, 3V3_SLEEP 3.298 V (3.2 .. 3.4 V): pass; U5 (TPS63802)
  regulates from the battery.
- Charging needs the bug-001 47 k (U1 is BQ24072T): TS 0.88 V, inside the 0.65-1.3 V window (~26 C) (2026-10-19).
- Charge current (2026-10-21, cell wired to C11): 0.94 A on the USB meter = 0.89 A into the cell + 51 mA board, the
  full set current, U1 not throttling; CV taper at 4.20 V (BAT 4.18 V at ~250 mA); terminates (D1 off).
- At 4.15 V a replug reports done at once (above the ~4.10 V recharge threshold); the charge ends by termination,
  not on the 6.3 h timer (2026-10-20).
- Every charger problem of 2026-10-19 .. 10-21 was the JB1 cell joint (mechanical), not U1 or the cell (2026-10-21).
- USB detected (PGOOD low) 8 of 8, K230 boots on the charger rail (run.py, 2026-10-25).

- D1 (/CHG) lights only during the first charge cycle after USB is plugged in. A top-up charge started later (the
  cell below the ~4.1 V restart threshold, e.g. after test105's full load) runs at ~0.44 A with D1 dark; a cell at
  4.15-4.17 V after such a run does not charge at all until it falls below the threshold (2026-10-30). "Full" for a
  current reading = the input back at the BL616-only level with the K230 off, not "D1 off".

Open:
- After ~1 h of charging 350 mA for a while (a replug started at 0.45 A, ramped to 350 mA), then 0.94 A again: not
  explained (2026-10-21).
- Charge time from empty to full not timed (the end of charge was never watched).

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, VBAT C15.1, BAT C11.1, 3V3_SLEEP (charging.png / battery_only.png).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.47 / 0.47 s (median / max of 8; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: the rails ramp in 1.7 ms (test003) (2026-10-25) | - | |
| Test run time | 169 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | n/a: power path test (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | rails off 0.061 A, K230 at msh idle 0.220 A (test006, 2026-10-25); charge currents: Result (2026-10-21) (2026-10-25, from recorded readings) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: power path test; the board's states are priced in test006/007/031 (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: as energy (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 37.0 / 37.0 / 34.9 C; board 29.5 / 29.5 / 28.2 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (power path, BQ24072 and regulators): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: rails-on time (3V3 up 1.7 ms after K230_PWR, measured in test003's pt_on) and RSTN delay: the floor of every boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: the battery-only rails' quiescent current (BL616 running the test firmware, K230 rails off and on); charging is from USB, n/a.
