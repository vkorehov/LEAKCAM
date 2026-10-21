# test021: K230 boot time

Status: **planned** (2026-10-21).

Every wake powers the K230 up from cold, so its boot time is paid on every session (the whole session: test101).
Not measured yet: the test firmware does not time it and the product BL616 firmware (which logs "READY <ms> after
power-on") does not run on rev 1 yet.

## Target

The design wants the fastest boot: it is paid on every wake (ENERGY.txt, the biggest lever). Estimate before the
first measurement (2026-10-21): 1-3.5 s power-on to READY. Target to set from the first measurement; first
candidates: sensor probes without cameras, the /sdcard UFFS mount, DDR training, the kernel load (NAND clock).

## Setup

BL616 test firmware; K230 NAND image (current test image or the product image); both consoles recorded with
timestamps (skill k230-rtsmart-hangs `console_record.py`, K230 console on the CH340; BL616 console on PR1).

## Steps

1. Rails off >= 5 s, then `pt_on` (the BL616 prints the 3V3 ramp), `pt_run` at t0 (reset released).
2. From the K230 console, the time of each milestone after t0:
   - U-Boot SPL banner (ROM read SPL from NAND);
   - "Firmware run has completed 2667" (DDR training done);
   - OpenSBI banner;
   - RT-Smart banner;
   - UFFS mounted (/bin, /sdcard), `msh />`;
   - agent `READY` on the link (the BL616's UART0 shows it with `pt_uart on`).
3. 5 cold boots: median and spread per step; name the longest step.
4. Repeat after each speed change that touches boot (NAND clock test016, DDR 3200 test024) to see what it bought.

## Pass

Recorded per step; the biggest steps named as candidates to shorten.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| boot steps (SPL, DDR, OpenSBI, RT-Smart, mounts, READY) | not measured yet | NAND 100 MHz, DDR 3200 (SPEEDS.txt 3) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

This test is the boot time measurement.

## Battery energy

Battery current through the boot, per step (scope over a shunt in the battery lead): energy per boot = the fixed cost of every wake. Also after each speed change (NAND clock, DDR 3200).
