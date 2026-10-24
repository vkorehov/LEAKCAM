# test038: battery brown-out below 3.10 V (cutoff, lockout, recovery)

Status: **cut + lockout + USB wake pass** (2026-10-23): K230 `bat_watch` cut (raised threshold), heartbeat stop, rails off, lockout HBN, USB plug wakes it with no EN; BL616 watchdog added (no hang stays). Below-3 V steps (the real 3.10 V cut, the 3.50 V unlock) need a lab supply (none).

Rev 1 has no hardware discharge cutoff (bug-004): the BQ24072 keeps its BAT-FET on, so the firmware is the only
guard. The K230 reads VBAT (test023) and reports it in the WAKE ACK; the BL616 keeps it in HBN RAM; below 3.10 V
under load it cuts the K230 at once and hibernates with only the USB wake (no RTC, no leak wake); the lockout ends
with USB plugged and the next reading >= 3.50 V. Below that the cell, U5 (3V3_SLEEP) and the BL616 brown out.

## Setup

**2026-10-22, quick test without a lab supply (user: no supply; the pack's own protection trips only at ~2.4-2.5 V,
bug-004, so the firmware cutoff stays the working guard):** the mechanism with a raised threshold instead of an
overnight drain. K230 bench `bat_watch [cut_mv] [log_s]` (VBAT from ADC_1 averaged over 64, logged; the GPIO2
heartbeat while VBAT >= cut, "CUT" and the heartbeat stopped after 3 readings below it); BL616 `pt_session <boot_ms>
lockout <stop_ms>`: supervised session, after the cut HBN with only the USB plug wake (no RTC: sleep time 0; leak
comparator wake disabled), as the product's bug-004 lockout. Board on the cell (USB-C 1 out).

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

## Results

### 2026-10-22, lockout (quick test)

| Step | Result |
|---|---|
| session on the cell, heartbeat stops (the agent's own end; bat_watch could not be started) | rails off 2 s / 60 s after the last edge, then **LOCKOUT** (22:33:52 and 22:36:21) |
| lockout, 3+ min with nothing | **no wake** (console stayed away: no RTC wake armed) |
| lockout, S1 shorted 3 s | **no wake** (the leak is ignored) |
| lockout, USB-C 1 plugged | **woke by usb** (twice: 22:34 and 22:40) |

### 2026-10-23 16:35, whole chain on the cell (fixed firmware: arm-then-check lockout, watchdog)

USB-C 1 out, `pt_session 10000 lockout 15000`, after the agent's hook `bat_watch 4150 5` on `/dev/ttyACM1` (cut
raised above the cell's voltage so it triggers now; the decision logic is what is tested, not the 3.10 V level).

| step | time | reading |
|---|---|---|
| unplugged -> session start | 16:35:39 | 3V3 up in 1709 us |
| first heartbeat (READY stand-in) | +1.5 s | |
| agent hook done | 16:36:03 (+24 s) | camera absent (cam tests skipped) |
| bat_watch readings | 0 / 5 / 10 s | VBAT **3947 / 4008 / 4009 mV** under load (cell not recharged) |
| CUT, heartbeat stopped | +10 s | 3 readings below 4150 |
| rails off | +48.9 s after power-on | 15 s heartbeat-stop limit |
| LOCKOUT HBN, off USB | 16:36:28 | |
| USB-C 1 plugged -> back on USB, "woke by usb" | 16:36:46 | no EN |

Pass. Energy: no current reading this run (meter not in the loop); session ~49 s at the earlier ~200 mA K230 figure
(ENERGY.txt) is ~2.7 mAh.

### 2026-10-23, bat_watch first readings

On the cell (USB-C 1 out), K230 running with its console on `/dev/ttyACM1`: `bat_watch` read **VBAT 4.084-4.091 V
under load**, falling ~1 mV per 10 s (09:43-09:45). The supervised session had not started (the rails were still on
from the boot-time runs: `pt_session` refuses then), so no cut was taken; the user plugged USB back to let the cell
charge. Next: `pt_off`, then the session with `bat_watch 4100 5` (cuts ~15 s in).

Not run: the K230-side cut decision (`bat_watch 4050 5`: VBAT read under load, CUT after 3 readings, heartbeat
stopped) - the K230 console (CH340, USB-C 2) did not reach Linux on this run (no VMware offer, user: the known
CH340/USB-C 2 problem). Now runnable without a click: the K230 console is `/dev/ttyACM1` (test039); `pt_session
10000 lockout 60000` on the cell, then `bat_watch 4050 5` on `/dev/ttyACM1` within 60 s of the agent's end. Steps 1-6 of the
plan (sweep, real cutoff, deep discharge, ramp) need a lab supply.

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

## Obstacles

- **2026-10-23, the board stayed in the lockout HBN while plugged in, until EN** -> `pt_session 10000 lockout 60000`
  on the cell, `bat_watch` cut at 09:46:09, the user plugged USB-C 1 back in during the session, and at 09:47:08 (60 s
  heartbeat limit) the BL616 turned the rails off and entered the lockout. VBUS came right after the rails went off
  (the user saw charging start then), before or while the plug wake (ACOMP0 falling edge on PGOOD) was armed ~50 ms
  later: no edge was left to wake it. USB plugged, BL616 gone from USB, PR1 replug no help, only EN. Fix (arm first,
  then look): the lockout now arms the edge and then checks PGOOD; USB already in = no HBN ("LOCKOUT skipped"), an
  edge after the arming stays pending and wakes HBN at once. `pt_sleep` likewise. The product had the same window
  (`sleep_for()` checked USB, then armed): `usb_arm_hbn_wake()` now arms and returns whether USB is in, and both sleep
  paths go to USB mode instead (firmware/bl616/usb_power.c, main.c). Retest 10:13: USB-C 1 in, `pt_session 10000
  lockout 3000` -> "LOCKOUT skipped: USB present", BL616 stayed on USB. **Open:** no charge (0.03 mA) while the K230
  ran with USB-C 1 replugged mid-session, charging only once the rails went off: the CH340 is off the board now, so
  not bug-002's D3 path; recheck by plugging during a session (pt_usb).
- **The BL616 dropped off USB again at 10:47:55 while awake in its shell** (lockout skipped at 10:24, no command
  sent; back only with EN at 16:25) -> cause not found: there was no watchdog, so any hang stayed for good. Fixed by
  the BL616 watchdog (firmware/bl616/wdg_guard.c, both firmwares): 8 s reset mode, fed every 1 s by a top-priority
  task. `pt_hang` (interrupts off, spin): off USB after 7.3 s, shell back after **12.9 s** with no hands, the boot
  says "reset by the watchdog". `make flash` with the watchdog running: fine (18 s, verified). If the drop happens
  again the boot line tells a hang (watchdog) from anything else.
- 16:27 retest: USB-C 1 plugged in during a session (K230 on): PGOOD low at once (charging), lockout skipped. With the
  CH340 off the board the mid-session plug works (the 0.03 mA case above is not reproduced).
- 16:28 retest of the real lockout with the fixed firmware: USB-C 1 out, `pt_session 10000 lockout 3000` -> LOCKOUT
  HBN 28 s after power-on (16:29:19, off USB); USB-C 1 plugged -> BL616 back on USB at 16:29:36 by itself, "woke
  by usb", no EN. Pass.
- **`make flash` waited for ever on the ROM** (2026-10-23) -> `bl616_isp.py` took the last ttyACM of the firmware, the
  second port (`/dev/ttyACM1`, the K230 console), and sent `reboot uart` to the K230. Fixed: it takes interface 0 only.
(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **No lab supply** -> the mechanism tested with a raised threshold on the real cell instead of the sweep.
- **The K230 console (CH340) did not reach Linux** while the K230 ran on the cell -> the lockout was tested on the
  agent's own heartbeat stop; the VBAT cut decision stays open.
- **bl616_isp.py --boot refused once** (the ROM's ISP session had timed out) -> new handshake built in (test002).
