# test101: session length (wake to power-off)

Status: **planned** (2026-10-21).

The whole session: wake, K230 boot (measured on its own in test021), READY, the agent's work, the heartbeat
stopping, power-off. With test102 it decides the battery cost of a wake.

## Steps

1. BL616 test firmware, console of both recorded with timestamps (`console_record.py`): `pt_on`, `pt_run` at t0;
   K230 milestones: SPL, OpenSBI, RT-Smart banner, agent READY on the link.
2. Repeat 10 times (cold, rails off >= 5 s between).
3. With the product BL616 firmware later: its own "READY <ms> after power-on" and the session end line.

## Pass

Recorded: rails-on -> READY (median, max), and what dominates it (DDR training, NAND read, mounts); first
candidates to shorten.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| session length per step | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

Boot is test021; this test adds the session after READY.

## Battery energy

Energy per whole session (with test102): the number the budget multiplies by the wakes per day.
