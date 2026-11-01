# test114: the lowest BLE energy at which the phone still finds the board

Status: **planned** (2026-11-01, user: how little energy can BLE use while the board waits to be discovered; and a
wake from the app for a board that is not advertising).

A board on battery spends its BLE energy waiting: either advertising (the phone hears it) or listening (it hears the
phone). This test finds, for each, the current against the time the phone needs to find the board, so the product can
pick its operating point (provision.c: today 60 s of advertising every 5 minutes while no network is stored; silent
once configured).

## Mechanisms measured

- **A, slow advertising:** connectable advertising at 100 ms, 500 ms, 1 s, 2 s, 5 s and 10.24 s (the BLE maximum);
  for each: the phone's time to first sight (BleOp scan, 10 tries), the time to connect, and the current.
- **B, wake beacon (the board listens):** the board scans passively in a short window every interval (window 10, 30 ms;
  interval 1, 2, 5 s); the app advertises a wake beacon (manufacturer data 0xFFFF, "LW" + the board's 6-byte device
  ID); on hearing it the board advertises for 60 s. For each duty: the beacon-to-discovery time and the current.
- **C, BLE sleep:** A and B with the BL616 in PDS between radio events (the BLE controller wakes it), if the SDK's
  low-power framework carries it on our controller library (CONFIG_LPAPP and the PDS hook, kept empty since test040's
  USB finding, firmware/bl616/ble_pairing.c): the step that makes the numbers worth having on battery.

## Setup

- BL616 test firmware: `pt_ble adv <ms>` (advertising interval), `pt_ble listen <window ms> <interval ms>` (wake-beacon
  scan, advertising off until a beacon), `pt_ble off`; the phone with BleOp (scan timing) and the app's wake beacon.
- Current: the USB-C 1 meter with the cell full (charging done: the reading is the board's draw); the user reads it on
  cue (LEAKCAM_METER=ask), else latency only. No battery-side probes (bench limits).

## Needs (build features and dependencies)

BLE observer role (built: CONFIG_BT_OBSERVER), adjustable advertising parameters, passive scanning with a duty cycle;
for C the SDK's PDS low-power path with BLE. Phone: BLUETOOTH_ADVERTISE for the wake beacon (Android 12+).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. A: each interval, 10 discoveries (latency median / max), the current once per interval.
2. B: each duty, 10 wakes from the app's beacon (latency median / max), the current once per duty.
3. C: the best two of A and B again with PDS sleep between radio events.
4. Choose: the product's battery mode (advertising interval or listening duty) from the energy per day against the
   time a user waits at the phone.

## Pass

A table of current against discovery time for each mechanism, and the product's choice written into provision.c and
DESIGN.md with its energy per day.

## Result

Not run (planned).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | **MISSING** | - | |
| discovery time against current (A, B, C) | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 meter, cell full |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | per discovery / per hour waiting |
| Per day | **MISSING** | ENERGY.txt line | waiting 24 h at the chosen point |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | **MISSING** | minimal image (rule) | |
| Compile flags | BL616 test firmware -Os -g, sections, --gc-sections | smallest code (rule) | |
| Features over the minimal image | BLE observer, the wake-beacon scan | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

The point of the test: energy per day of waiting to be discovered, for each mechanism (ENERGY.txt).
