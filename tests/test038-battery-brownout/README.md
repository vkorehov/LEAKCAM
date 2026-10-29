# test038: battery brown-out below 3.10 V (cutoff, lockout, recovery)

Status: **partial** (2026-10-23): with a raised threshold on the cell the chain works - K230 `bat_watch` cut,
heartbeat stop, rails off, lockout HBN, USB plug wakes it with no EN; the real 3.10 V cut, the 3.50 V unlock and the
below-3 V steps need a lab supply (none).

Rev 1 has no hardware discharge cutoff (bug-004): the BQ24072 keeps its BAT-FET on, so the firmware is the only
guard. The K230 reads VBAT (test023) and reports it in the WAKE ACK; the BL616 keeps it in HBN RAM; below 3.10 V
under load it cuts the K230 at once and hibernates with only the USB wake (no RTC, no leak wake); the lockout ends
with USB plugged and the next reading >= 3.50 V. Below that the cell, U5 (3V3_SLEEP) and the BL616 brown out.

## Setup

**Quick test without a lab supply** (the pack's own protection trips only at ~2.4-2.5 V, bug-004, so the firmware
cutoff stays the working guard): the mechanism with a raised threshold instead of an overnight drain. K230 bench
`bat_watch [cut_mv] [log_s]` (VBAT from ADC_1 averaged over 64, logged; the GPIO2 heartbeat while VBAT >= cut, "CUT"
and the heartbeat stopped after 3 readings below it), on the K230 console `/dev/ttyACM1` (test039); BL616
`pt_session <boot_ms> lockout <stop_ms>`: supervised session, after the cut HBN with only the USB plug wake (no RTC:
sleep time 0; leak comparator wake disabled), as the product's bug-004 lockout. Board on the cell (USB-C 1 out).

Planned with a lab supply (not available): lab supply in place of the cell (JB1, current limit 1 A, 4.2 V start), USB out; meter on VBAT (C15) and on the
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

## Result

- Whole chain on the cell, cut raised to 4150 mV (`pt_session 10000 lockout 15000`, `bat_watch 4150 5`): VBAT
  3947 / 4008 / 4009 mV under load (cell not recharged); CUT and heartbeat stopped after 3 readings (+10 s); rails off
  +48.9 s after power-on (15 s heartbeat-stop limit); LOCKOUT HBN off USB; USB-C 1 plugged -> "woke by usb", no EN.
  3V3 up in 1709 us at session start (2026-10-23 16:35).
- The lockout ignores everything but USB: no wake in 3+ min (no RTC armed), S1 shorted 3 s -> no wake (2026-10-22).
- The lockout arms the USB wake edge (ACOMP0 on PGOOD) first, then checks PGOOD: USB already in -> "LOCKOUT skipped",
  BL616 stays on USB; the product's `usb_arm_hbn_wake()` arms and returns whether USB is in (2026-10-23, retests
  10:13 and 16:28).
- USB-C 1 plugged mid-session (K230 on): PGOOD low at once, charging, lockout skipped (2026-10-23 16:27).
- BL616 watchdog (8 s reset, fed every 1 s by a top-priority task) recovers a hang: shell back after 12.9 s, boot
  says "reset by the watchdog" (2026-10-23, `pt_hang`).
- Open: steps 1-6 (sweep, the real 3.10 V cut +-50 mV, 3.50 V unlock, deep discharge to 1.6 V, ramp back, load dip)
  need a lab supply.
- Open: the BL616 dropped off USB at 10:47:55 on 2026-10-23 while awake in its shell; cause not found (the watchdog
  now resets a hang and the boot line tells which).
- Open: no current reading in the chain run; the session (~49 s) is ~2.7 mAh only at the earlier ~200 mA K230 figure.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - lab supply in place of the cell, its current; voltage - VBAT C15.1, BAT C11.1, 3V3_SLEEP (U5 output) while ramping.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| cut-off voltage vs set 3.10 V, time to rails off | **MISSING** | +-50 mV | % of the top |
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

After a brown-out the BL616 cold-starts (HBN RAM lost): its boot to sleep, and any K230 boot it triggers.

## Battery energy

Lockout current (should be the sleep floor), and the charge left at the cut: the rest voltage after the cut ->
state of charge (ENERGY.txt section 1, usable charge).
