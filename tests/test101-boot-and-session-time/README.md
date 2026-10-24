# test101: session length (wake to power-off)

Status: **planned** (2026-10-21).

The whole session: wake, K230 boot (measured on its own in test022), READY, the agent's work, the heartbeat
stopping, power-off. With test102 it decides the battery cost of a wake.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. BL616 test firmware, console of both recorded with timestamps (`console_record.py`): `pt_on`, `pt_run` at t0;
   K230 milestones: SPL, OpenSBI, RT-Smart banner, agent READY on the link.
2. Repeat 10 times (cold, rails off >= 5 s between).
3. With the product BL616 firmware later: its own "READY <ms> after power-on" and the session end line.

## Pass

Recorded: rails-on -> READY (median, max), and what dominates it (DDR training, NAND read, mounts); first
candidates to shorten.

## Results

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| session length per step | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Boot is test022; this test adds the session after READY.

## Battery energy

Energy per whole session (with test102): the number the budget multiplies by the wakes per day.

## Obstacles

None yet.
