# test116: the whole setup over BLE alone (no USB)

Status: **pass** (2026-11-03 20:28-20:32 UTC, 16/16, run.py on the product pair, the phone 8 m away in another room):
unplugged -> asleep, found in 3.5 s, set up on battery in 12.6 s, the full firmware, present, silent on battery;
erased on battery -> the setup firmware started on the cell, found 31 s after the erase (the full firmware's next
check-in + the start), set up in 18.6 s, the full firmware, present; USB back in: the full firmware, silent, present.
bugs/bug-006 fixed for it (the sleep veto and a PDS reset at the setup firmware's start).

A new LEAKCAM set up from the app with USB-C 1 unplugged, on its battery: no USB power, no USB data. With no network
stored it runs the setup firmware, asleep in PDS, advertising every 2 s; the app finds it, pairs with the PIN from the
server and sets the network; the stored network switches it to the full firmware, which has no BLE.

## Setup

- The product pair (firmware/bl616_full + bl616_setup) with its pairing PIN stored, the cell charged.
- The phone with the app on adb, next to the board for the battery steps.
- Hands: USB-C 1 unplugged (`echo unplugged > ~/.leakcam_hand`, or `run.py --unplugged` with LEAKCAM_DEV when it is
  already out) and plugged back at the end; the BL616 console stays up on PR1 except while the setup firmware sleeps.

## Needs (build features and dependencies)

As test106: BLE provisioning, the Wi-Fi check, presence; the advertising windows (provision.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. USB in: the network erased (`provision erase`): the setup firmware.
2. USB-C 1 out (hand): asleep, found by the phone; set up from the app on battery: the full firmware, present.
3. Configured, on battery: silent (30 s scan).
4. Erased on battery: the full firmware starts the setup firmware without USB; found, set up again.
5. USB-C 1 back in (hand): the full firmware, silent, present.

## Pass

A new board is set up from the app over BLE alone, on its battery, within one advertising period plus the setup.

## Result

Not run (planned).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 119 s automated (run.py, 2026-11-03), no setup | - | |
| setup on battery: unplug -> found -> Done | found 3.5 s after the unplug, app opened -> Done 12.6 s; started on battery: found 31 s after the erase, Done 18.6 s; the full firmware 4 s after Done (2026-11-03) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: on battery (no battery-side probes, bench limits) | floor (test031) | |
| Voltage under load (min) | n/a: no battery-side measurement on the bench (memory: bench limits) | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on the cell, no meter on the battery side; the setup firmware asleep is test120's | - | |
| Per day | n/a: once per installation | ENERGY.txt line | waiting for setup: test114 |
| Temperature die / board (start / peak / end) | n/a: K230 off, the BL616 at idle load | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: each switch is a fresh boot (no heap carries over); one sample after the run (HISTORY.md) | no leak | |
| Image (sizes) | BL616 test firmware 1322032 B (2026-11-03, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 product -O3 + LTO | smallest code (rule) | |
| Features over the minimal image | as test106 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

The setup itself on battery: from the board's own counters when the energy tests (test114, test108) give the costs.
