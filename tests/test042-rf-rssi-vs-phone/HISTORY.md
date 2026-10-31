# test042: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

- 2026-10-31: position 1, 10 runs (README Result). The first run was restarted after the user removed an aluminium
  tube lying next to the board. Then 8 "hang hunt" rounds (each from a fresh BL616 boot) with the crash recorder.

Created 2026-10-31 at the user's request (an RF estimate with the phone beside the board, per channel);
the phone's `list-scan-results` line format is parsed as on Android 11+ and checked on the first run.

## Obstacles

- **BL616 watchdog reset in the first run (2026-10-31 ~18:58), not reproduced.** The phone heard the board in 0 of
  10 BLE scans; the board said "this boot: reset by the watchdog". The Wi-Fi breadcrumbs (pt_trace): 20 scans done
  (0x49), then nothing: the hang came at or right after `pt_ble on` (BLE starting while the Wi-Fi radio was up).
  Not reproduced: BLE on with the radio up, phone scans with Wi-Fi up, a fresh boot + 10 scans + BLE on, the full run
  9 more times. Conditions of the failing run not repeated: 59 min uptime, the stopped `pt_leak log` task, a PR1
  unplug / replug, and the run before it killed by me mid-scan (the aluminium tube). Since then the firmware has the
  crash recorder (firmware/bl616/crash_rec.c) and BLE start breadcrumbs 0x70-0x75 / 0x7e-0x7f: a recurrence gives a
  backtrace.
- The phone left USB at 19:28 (cable); the first hang-hunt start found no phone and stopped; restarted 19:55.
- The VM slept 491 s during hunt round 1 (until 20:05:32): that round took 10 min; the BL616 survived the frozen
  USB host without a reset or a crash record.
