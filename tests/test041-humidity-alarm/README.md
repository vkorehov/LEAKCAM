# test041: humidity alarm (AHT20 threshold wakes the K230)

Status: **dropped** (user, 2026-11-06): no humidity rule, wake or alarm in the product (DESIGN.md "Humidity"). On the
board the AHT20 reads the board's heat (42.6 C in a 21 C room) and skin vapour moves it (test048 steps 2-3): useless as
a leak judge. The AHT20 stays reported only; rev 2 moves it off the board (bugs/v2.txt 12). Kept for the record.

The BL616 reads the AHT20 on every RTC slice (every 10 min, ~85 ms awake, K230 off). At **85.0 %RH or more**
(`HUM_ALARM_X10`, attic/firmware/bl616/main.c) it powers the K230 with wake reason `humid`; `leakcam_wake` takes it as a
sensor alarm and reports to the server with the reading. There is no memory of an earlier alarm: every slice above the
threshold wakes the K230 again (DESIGN 5.1).

## Setup

- A sealed plastic box with a saturated salt solution in a dish (no water on the board): **KCl ~85 %RH** (at the
  threshold), **NaCl ~75 %RH** (below), K2SO4 ~97 %RH (well above); the board on a stand above the dish, room
  temperature steady. A reference hygrometer in the box.
- Battery (cell full) for steps 3-6, USB-C 1 out (USB mode never takes the humidity path); the BL616 console on PR1.
- A mock server on the PC for step 5 (written with the product rewrite; the first one, attic/firmware/k230_capture/mock_server.py, is reference only).

## Needs (build features and dependencies)

BL616: AHT20 driver, RTC HBN wake, the humidity slice and the alarm decision (main.c), the K230 power path and the
WAKE reason; K230: agent + `leakcam_wake` (sensor alarm path), Wi-Fi bridge (test100) for the report.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Accuracy (test firmware, `pt_status` AHT20): the reading against the reference at room air, NaCl and KCl, each
   after 30 min settling: offset at 75 and 85 %RH (the threshold's real position).
2. Response: the time from the box closing (KCl) to the reading crossing 85.0 %RH.
3. Threshold (test firmware with the humidity slice): above 85.0 the next slice wakes the K230 with reason `humid` (latency <= 10 min plus
   the K230 boot); below (NaCl) no slice powers the K230 (evlog counts).
4. Sustained alarm: 1 h at KCl/K2SO4 - a K230 session every slice (6 an hour), each reported; the energy per hour this
   costs (Battery energy). Out of the box: the wakes stop at the first slice below.
5. The report: `/v1/check` carries `reason=...humid`, `rh` and `t`; the mock answers; `leakcam_wake` sleeps as the
   answer says.
6. Faults: the AHT20 not answering (I2C held / sensor removed) gives no alarm and no stuck wake loop, logged; exactly
   85.0 %RH counts as alarm.

## Pass

Every slice at or above 85.0 %RH wakes the K230 and reaches the server with the reading; none below does; the
reading is within the AHT20's +-2 %RH of the reference at 75-85 %RH; a sensor fault never wakes the K230 by itself.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | **MISSING** | target (test022) | longest step |
| This part's boot share | **MISSING** | - | driver / mount / probe |
| Test run time | **MISSING** | - | setup separately |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | **MISSING** | - |  |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) |  |
| Features over the minimal image | **MISSING** | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

The humidity slice's own time (RTC wake -> AHT20 -> back to HBN, test021: AHT20 36.5 ms) and, on an alarm, wake ->
K230 READY (test022 / test101).

## Battery energy

Per alarm hour: 6 K230 sessions (test102's session energy) against the humidity-slice floor (test032); the budget
line in ENERGY.txt for a room that stays humid (bathroom, leak under a floor).
