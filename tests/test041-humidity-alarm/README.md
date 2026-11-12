# test041: humidity alarm and room readings with the real AHT20 (low level: the sensor on the board)

Status: **restored 2026-11-10** (user: "restore humidity and temperature use case"; dropped 2026-11-06, removed in
1b9f539). Not run yet. The product flow with a simulated humidity is test140 (hands-free); this test proves the real
sensor: its accuracy, its response, and that a warm board only reads lower humidity (DESIGN.md 5.3.15).

The full firmware reads the AHT20 every 10 min (climate.c). **85.0 %RH or more** at any reading is a humidity alarm
(`ALARM_X10`): a check-in at once (the server's leak `humidity high`) and a K230 leak check with the reason `humid`.
The room's temperature and humidity only after the board was cool for 3 h (no K230, no USB power). No memory of an
earlier alarm: every reading at or above the threshold does it again.

## Setup

- A sealed plastic box with a saturated salt solution in a dish (no water on the board): **KCl ~85 %RH** (at the
  threshold), **NaCl ~75 %RH** (below), K2SO4 ~97 %RH (well above); the board on a stand above the dish, room
  temperature steady. A reference hygrometer and thermometer in the box.
- Battery for steps 3-6 (USB-C 1 out by the relay box: on USB power the board is warm); the BL616 console on its own
  port; the server on the Pi.

## Needs (build features and dependencies)

BL616 full: aht20.c, climate.c (the reading, the window, the alarm), leak_check_now(), the K230 session; K230: the
job's leak check; server: leakIn `humidity high`.

## Steps

1. **Accuracy** (`climate now`): the reading against the reference at room air, NaCl and KCl, each after 30 min
   settling, on battery with the board cool: offset at 75 and 85 %RH (the threshold's real position) and the
   temperature against the thermometer.
2. **Response:** the time from the box closing (KCl) to the reading crossing 85.0 %RH.
3. **Warm board reads lower** (the reason the alarm needs no window): in the KCl box, USB power in (K230 on, charging)
   for 30 min, then out: `t` up and `rh` down while warm, both back once cool; the cool readings match step 1.
4. **Threshold:** above 85.0 the next 10-min reading raises the alarm (`climate: humidity ... alarm`, a K230 session
   `humid`, the server's leak); below (NaCl) none (`climate`: alarms 0).
5. **Sustained:** 1 h at K2SO4 - an alarm at every reading (6 an hour), each a K230 check; the energy per hour this
   costs (Battery energy). Out of the box: the alarms stop at the first reading below.
6. **The room after 3 h:** the board on battery, untouched, 3 h: the reading after it is the room's (`troom`/`rhroom`
   in the check-in) and within the AHT20's +-0.3 C / +-2 %RH of the reference.
7. **Faults:** the AHT20 not answering (I2C held / sensor removed): `climate` counts fails, no alarm, no room reading,
   no stuck loop; exactly 85.0 %RH counts as alarm (server TestHumidityLeak).

## Pass

Every reading at or above 85.0 %RH opens the server's leak and runs a K230 check; none below does; a warm board reads
lower humidity, never higher; the cool reading is within +-2 %RH / +-0.3 C of the reference at 75-85 %RH; a sensor
fault never raises an alarm.

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

The reading's own time (awake firmware: ~37 ms AHT20 conversion; with HBN: RTC wake -> AHT20 -> back to HBN, test021:
AHT20 36.5 ms) and, on an alarm, the reading -> the K230's DONE (test101).

## Battery energy

Per alarm hour: 6 K230 sessions (test102's session energy) against the 10-min reading floor (DESIGN 5.3.12: ~0.02
mAh/day for 144 readings); the budget line in ENERGY.txt for a room that stays humid (bathroom, leak under a floor).
