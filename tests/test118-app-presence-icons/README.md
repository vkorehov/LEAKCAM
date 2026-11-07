# test118: the app's icons follow the board (BLE, presence, red)

Status: **pass** (2026-11-01, 7/7; user: "device looses ble, ble icon disappears, looses wifi, wifi icon disappears and we
need to mark device as red which has no presence anymore").

The app's status row for a LEAKCAM: the Bluetooth icon while the board is in BLE range (its Wi-Fi can be set), the
Wi-Fi icon while it is present at the server, the row's text red while it has no presence (offline, never seen, no
server). The app keeps a board in range while it was seen in the last 45 s (it scans every 30 s), so the icon does not
blink at every rescan. Every redraw of a row is logged ("row <id> wifi 0|1 ble 0|1 red 0|1").

## Setup

- BL616 test firmware (tests/firmware/bl616_powertest: `pt_ble adv off|on`), the PIN stored; USB-C 1 in; the phone on
  adb, unlocked.
- The test firmware has no presence of its own: the bench checks in at the server under the board's own ID (as the
  board would) every 10 s for the run; the runner sets the board's check-in interval to 10 s per device with `PUT
  /v1/devices/<id>/intervals` (phone.intervals: the server's offline mark is three missed intervals) and sets 60 s
  back at the end (product default 3600 s, the bench server's default 60 s).

## Needs (build features and dependencies)

BLE advertising (ble_pairing.c); the presence server.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. In range and present: both icons, not red.
2. Advertising off: the Bluetooth icon goes; the Wi-Fi icon stays, not red.
3. Check-ins stop (three intervals): the Wi-Fi icon goes, the row red.
4. Check-ins again: the Wi-Fi icon back, not red.
5. Advertising on: the Bluetooth icon back.
6. Both off at once: no icon, red.

## Pass

Every change shows in the row within one scan or presence period plus the server's three missed intervals.

## Result

2026-11-01, 7/7 checks: start both icons, not red (11 s after the app opened); advertising off -> the Bluetooth icon
gone after 51 s (45 s unseen + the next 30 s scan), Wi-Fi stays; check-ins stopped -> Wi-Fi icon gone and the row red
after 29 s (three 10 s intervals); check-ins again -> Wi-Fi back in 11 s; advertising on -> Bluetooth back in 20 s;
both off -> no icon, red in 61 s.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 217 s automated (run.py, 2026-11-01), no setup | - | |
| each change -> its icon | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | |
| Per day | n/a: an app test | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 29.3 / 29.3 / 29.3 C (AHT20) (2026-11-01) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 151448 / min 146720 / 151448 B, blocks 61 -> 61, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak | |
| Image (sizes) | BL616 test firmware 1317888 B (2026-11-01, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 test firmware (no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | BLE only | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

n/a: an app test on USB power.
