# test129: the app shows the board's power and signal as icons

Status: **new, not run yet** (2026-11-04, user: "for usb power show plug icon, show battery otherwise; for signal
strength show strength indicator instead of decibels, decibels on hover"). Design: DESIGN.md 5.3.4.

The setup firmware advertises its USB power in the flags byte (bit 1; bit 0: a network stored), read when advertising
starts. The app's row for a board in BLE range shows a plug icon on USB power, a battery icon otherwise, and the
signal as 1-4 bars (>= -60 dBm 4, >= -70 3, >= -80 2, else 1); a long press on the bars shows the dB value. The USB
link icon (the app's own USB link with the board) comes with the USB setup (DESIGN.md 5.3.4, not built).

## Setup

- The product pair (firmware/bl616_full + bl616_setup) with the real network stored, USB-C 1 in.
- The phone on adb, the app and BleOp installed (`ready()`).
- `--battery`: two hand steps (USB-C 1 out, back in), each answered by `echo <word> > ~/.leakcam_hand`.

## Needs (build features and dependencies)

BL616: ble_pairing.c (flags bit 1, ble_pairing_usb_power()), setup_sleep.c (PGOOD). App: MainActivity.kt (barCount,
the plug / battery icons, the long press, the "row" log line).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. `provision erase` on the full firmware: the setup firmware on USB power.
2. The app's status screen with only this board: its row shows the plug icon; the bars match the dB value.
3. A long press on the bars: the dB value (logged "dbm <n>").
4. `--battery`: USB-C 1 out -> the battery icon (the unplug restarts advertising with bit 1 clear); back in.
5. The real network set from the app again: the full firmware (the bench as it was).

## Pass

Every check; run.py prints them.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 602 s automated (run.py, 2026-11-04), no setup | - | |
| each change -> its icon | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | |
| Per day | n/a: an app test | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | BL616 test firmware 1322032 B (2026-11-04, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 product pair (no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | BLE only | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

n/a: an app test (on battery only in `--battery`, minutes).
