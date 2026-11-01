# test116: the whole setup over BLE alone (no USB)

Status: **planned** (2026-11-01, user: the same as the USB-only setup, test113, with BLE only and no USB).

A new LEAKCAM set up from the app with USB-C 1 unplugged, on its battery: no USB power, no USB data. Since the USB
power gate is gone (2026-11-01), the board advertises on battery 60 s every 5 minutes while no network is stored; the
app finds it, pairs with the PIN from the server and sets the network.

## Setup

- Product firmware (firmware/bl616) with its pairing PIN stored (make flash), the cell charged.
- The phone with the app on adb (its own USB to the bench is the phone's, not the board's).
- Hands: USB-C 1 unplugged at the start (the board's console stays up on PR1 for the checks), plugged back at the end.

## Needs (build features and dependencies)

As test106: BLE provisioning, the Wi-Fi check, presence; the advertising windows (provision.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. USB in: the board's network erased (`prov erase`): a new board.
2. USB-C 1 out (hand): on battery.
3. The app finds the board within one period (5 minutes) and sets it up: pairing with the PIN (no prompt), check,
   Done; stored; the board checks in at the server; the app lists it online with its network.
4. Configured, on battery: silent for a whole period (no advertising).
5. USB-C 1 back in (hand): advertising again.

## Pass

A new board is set up from the app over BLE alone, on its battery, within one advertising period plus the setup.

## Result

Not run (planned).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | **MISSING** | - | |
| setup on battery: unplug -> found -> Done | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: on battery (no battery-side probes, bench limits) | floor (test031) | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | |
| Per day | **MISSING** | ENERGY.txt line | waiting for setup: test114 |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | **MISSING** | minimal image (rule) | |
| Compile flags | BL616 product -O3 + LTO | smallest code (rule) | |
| Features over the minimal image | as test106 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

The setup itself on battery: from the board's own counters when the energy tests (test114, test108) give the costs.
