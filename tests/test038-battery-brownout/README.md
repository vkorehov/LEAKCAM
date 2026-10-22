# test038: battery brown-out below 3.10 V (cutoff, lockout, recovery)

Status: **planned** (2026-10-21).

Rev 1 has no hardware discharge cutoff (bug-004): the BQ24072 keeps its BAT-FET on, so the firmware is the only
guard. The K230 reads VBAT (test023) and reports it in the WAKE ACK; the BL616 keeps it in HBN RAM; below 3.10 V
under load it cuts the K230 at once and hibernates with only the USB wake (no RTC, no leak wake); the lockout ends
with USB plugged and the next reading >= 3.50 V. Below that the cell, U5 (3V3_SLEEP) and the BL616 brown out.

## Setup

Lab supply in place of the cell (JB1, current limit 1 A, 4.2 V start), USB out; meter on VBAT (C15) and on the
supply current. The BL616's USB console needs USB, so log through the K230 console and read the BL616's HBN RAM
state after recovery.

## Needs (build features and dependencies)

BL616 power path, bug-004 cutoff (product), K230 ADC + agent (WAKE ACK).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Sweep down in 50 mV steps from 3.6 V with sessions running: the reported VBAT against the meter (error, test023);
   the cut at 3.10 V under load (the step where it fires, time to rails off).
2. Lockout: after the cut, no RTC or leak wake for 1 h (current = sleep floor, test031); a leak on S1 stays ignored.
3. Recovery: USB plugged at 3.3 V -> stays locked; supply raised to 3.50 V with USB -> unlocks, normal wakes resume.
4. Below the cutoff, firmware no longer in control (supply down to 2.5, 2.0, 1.6 V): U5's output, the BL616's reset
   (restarts cleanly or latches up), the K230 rails stay off, no NAND write in flight (test028).
5. Slow ramp back up from 1.6 V to 3.7 V without USB: the BL616 boots once, HBN RAM lost -> cold start with no
   lockout: does it start a session on a nearly empty cell? (expect: the first reading decides).
6. Load dip: a session start (Wi-Fi + LEDs) at 3.15 V rest: the sag under load that trips the cut.

## Pass

Cut at 3.10 V +-50 mV under load, every time; lockout holds without USB; unlock only at >= 3.50 V with USB; no
latch-up or NAND damage down to 1.6 V and back.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - lab supply in place of the cell, its current; voltage - VBAT C15.1, BAT C11.1, 3V3_SLEEP (U5 output) while ramping.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| cut-off voltage vs set 3.10 V, time to rails off | not measured yet | +-50 mV | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

After a brown-out the BL616 cold-starts (HBN RAM lost): its boot to sleep, and any K230 boot it triggers.

## Battery energy

Lockout current (should be the sleep floor), and the charge left at the cut: the rest voltage after the cut ->
state of charge (ENERGY.txt section 1, usable charge).

## Obstacles

None yet.
