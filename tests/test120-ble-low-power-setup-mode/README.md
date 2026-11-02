# test120: the BLE low-power setup mode alone (PDS + lpfw advertising, USB wake)

Status: **in progress**: BLE sleep with 2 s advertising works on rev 1, the product's BLE runs in it, the phone finds
the sleeping board 10/10 (2026-11-01); the connection wake, the USB wake, other intervals and the battery-side current
still open (TODO below). Rules learned: skill bl616-ble-low-power. Started 2026-11-01 (user: "if we have no working presence always use that low power mode ... USB should
also be supported in this mode, so we can use it as a fallback to ble"; priority). Design: DESIGN.md 5.3.1.

The low-level proof of setup mode on rev 1, on the test firmware: the BL616 sleeps in PDS (RAM kept) while the SDK's
low-power firmware (lpfw, `CONFIG_LPAPP`, from bouffalo_sdk examples/pmu/wl_ble_lp) sends connectable advertisements;
a phone connecting wakes the app; USB-C 1 plugged in (BQ24072 PGOOD as a PDS wake pin) wakes it too. The vendor's kit:
PDS 50 uA, advertising every 2 s 428 uA, every 500 ms 1430 uA (TX 0 dBm). The bench console (the BL616's USB on
PR1) drops while the chip sleeps: the runner reads the counters after each wake.

## Setup

- The setup firmware's test bed tests/firmware/bl616_setup_test (firmware/bl616_setup's sources, lpfw concatenated by
  the build; `lp_boot`, `wakeup_timer <ms> [usb]`, `lp_sleepstat`, `lp_pwr`), the PIN stored.
- The phone on adb, unlocked. USB-C 1 in, the cell in and charged; the meter on USB-C 1 on cue (LEAKCAM_METER=ask).
- `wakeup_timer <ms> usb` sleeps with USB power present (the product does not: the full firmware's case), so the
  mode runs and is metered on the bench; the USB wake is test121.

## Needs (build features and dependencies)

BLE (ble_pairing.c), `CONFIG_LPAPP` + lpfw (bouffalo_sdk components/wireless/lpfw_private, tools/lpfw), the power
manager (components/os/power_mgmt: tickless, rc32k_trim), PDS IO wake (PGOOD). Wi-Fi off in this test.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. Build check: the image carries lpfw (version string), the RC32K auto calibration runs, `pt_lp status`.
2. PDS floor: `pt_lp enter 0` (no BLE), 60 s, wake by timer: the framework's time in PDS / lpfw / app and its
   predicted current; the meter.
3. Advertising in low power at 2 s, 1 s, 500 ms: `pt_lp enter <ms>`; the phone scans 10 times (spaced 7 s): found
   count, median, max; after a timer wake the lpfw advertising counters (sent, lost) and the predicted current.
4. A phone connecting wakes the app: the app's setup connects, pairs with the PIN and reads STATUS; the board is
   awake (console back) within the connection; disconnected, it goes back to low power by itself.
5. A full setup from the app out of low power (Done) and back to low power after it.
6. Leaks: 20 enter/wake cycles, BL616 heap start/end.
7. By hand (one plug): USB-C 1 out, `pt_lp enter 2000 usb`; USB-C 1 in wakes it (console back, wake reason IO).

## Pass

The phone finds the board every time at the chosen interval, a connection and a USB plug wake it, a full setup runs
from low power, and the measured current per day fits a months-long wait on one cell.

## Result

- 2026-11-01, test firmware tests/firmware/bl616_setup_test (then named bl616_lp; `lp_boot`, `ble_start_adv 0 0 0x0c80 0x0c80`, `wakeup_timer
  120000 0`, `lp_sleepstat`, `lp_pwr`), TX 13 dBm, USB console off for the window, watchdog feeding held:
  - the BL616 sleeps between advertisements: 61 PDS sleeps in 120 s (one per 2 s advertisement), 119.3 s in PDS,
    0.7 s awake in all (~12 ms per wake); the SDK's prediction from that split 419 uA (its fixed PDS 80 uA / app
    57 mA figures, not our board's).
  - USB-C 1 meter 1290-1300 uA, with short peaks now and then (the advertising wakes); awake with the same
    advertising 61 mA. The USB path's floor is 1.23 mA with the board asleep (test031: mostly the charger's own input
    current), so this reads only ~60-70 uA over that floor and **does not give the battery current**.
  - battery-side estimate (calculation): board hardware ~26 uA (test031) + BL616 PDS ~50 uA (SDK) + awake share
    ~340 uA = ~420 uA, ~10 mAh/day; the awake time per advertisement is the lever.
  - **working figure until the battery measurement: the vendor spec, 428 uA at 2 s advertising (~10.3 mAh/day)**
    (user, 2026-11-01: "i would trust spec for now"); the calculation above agrees.

- 2026-11-01 20:01 UTC, the product's BLE (ble_pairing.c: advertising as the device ID with the provisioning
  service, PIN pairing) on the setup firmware, every 2 s: the phone (BleOp `scan`) found it 3/3 awake and **10/10 in a
  120 s sleep window, median 1.14 s, max 3.66 s** (RSSI -33 to -53); the board in PDS 119.3 of 120 s, 62 sleeps,
  SDK prediction 434 uA; the USB console gone in the window (really asleep).
- 2026-11-02 05:41-05:45 UTC, regression after the firmware split (PR #7; the setup test bed now builds
  firmware/bl616_setup's sources): `lp_boot` -> "lp: boot done", PIN stored; the phone found it 3/3 awake and **8/8 in
  a 60 s sleep window** (USB power present: `wakeup_timer 60000 usb`); 59.6 of 60.0 s in PDS, 32 sleeps (one per 2 s
  advertisement), SDK prediction 471 uA. A first try ended 9 s in: the test bed's `wakeup_timer` without `usb` had
  refused its window but still started its timer, which then ended the next window (fixed: refused before the timer).
- **BLE core wedge** (the hard rule of this test): with BLE up but idle (advertising off, no connection) the sleep
  path's controller-deadline call left the BLE core asleep for good; after any software reset the BLE init hung.
  Cases: A no sleep -> fine, B advertising window -> fine, C advertising off without sleep -> fine, D advertising off +
  sleep window -> wedged, only EN cleared it. Fixed in the idle hook (`ble_pairing_sleep_ok()`): D passes. Details:
  HISTORY.md.

## TODO

- **Battery-side current of BLE sleep: measure on the next board to be assembled** (user, 2026-11-01: not on this unit,
  the battery-lead wiring is too involved here): meter in the cell's + lead as in test031 Setup, cables out, the
  same window (2 s advertising) and pure PDS (`ble_stop_adv` first); then 1 s and 500 ms, TX 13 vs 0 dBm. Until then
  the per-day figure is the calculation above.
- USB-C 1 reading of the pure-PDS window (no advertising) against the 1300 uA: the advertising cost on the same path.
- Step 3 at 1 s and 500 ms (2 s done); steps 4 (phone connection wakes), 5 (full setup from low power), 6 (leak
  cycles), 7 (USB plug wake) and run.py.
- A start deadline for the crash recorder (a single stuck task while the rest runs is not caught: the hung BLE start
  went unrecorded for an hour), with the stuck task's saved PC and stack.
- The experiment commands in tests/firmware/bl616_setup_test (`lp_blecore`, `lp_pds`, `lp_reset`) out once the wedge rule has
  held through the higher-level tests.
- Setup and operational as two firmwares in the two flash slots, switched at runtime (user agreed 2026-11-01:
  presence lost -> setup slot, presence established -> operational slot): a slot-switch test, shared settings and a
  fixed crash-record address in both.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 163 s automated (run.py, 2026-11-01), no setup | - | |
| advertising interval -> discovery time (phone) and current | presence lost -> advertising: see the CHECK lines; set up again from the app 0.0 s | - | % of the top |
| Current idle / during (min / avg / peak) | USB-C 1: 2 s advertising in PDS 1290-1300 uA avg, short peaks; awake 61 mA (2026-11-01); battery side **MISSING** (next assembled board) | floor 35 uA battery / 1.23 mA USB (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | |
| Per day | n/a: setup mode, waiting days | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 145288 / min 120104 / 145288 B, blocks 107 -> 107, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak | |
| Image (sizes) | BL616 test firmware 1317888 B (2026-11-01, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 test firmware (no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | BLE + lpfw (CONFIG_LPAPP), Wi-Fi off | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

The current of the whole setup mode per day: PDS floor + lpfw advertising (the framework's own prediction from the time in PDS / lpfw / app, and the USB-C 1 meter with the cell full) -> ENERGY.txt.
