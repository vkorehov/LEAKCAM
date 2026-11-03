# bug-006: the setup firmware's BLE never starts with USB-C 1 out

Found: 2026-11-03, test116 (the whole setup over BLE alone). Status: open, cause not found.

## What happened

The setup firmware started without USB-C 1 power (the cell only) hangs in the BLE controller's start: the task
`ble_start` spins in `rwip_time_get` (ble_pairing_start -> btble_controller_init -> rwip_init -> rwip_rand_init),
the board never advertises. Every later start spins too, also after `reboot`, a watchdog reset and the ROM's
power-on reset (`isp`); only EN clears it (BOOT + EN by hand).

| BLE start of the setup firmware | USB-C 1 | Result |
|---|---|---|
| tests 040, 106, 110, 117, 119 (2026-11-02/03, dozens of starts) | in | always fine |
| `reboot` over PR1 after an unplug (2026-11-03 06:30 UTC) | out | spin; crash #134 "watchdog hang in task 'ble_start' at 6001 ms" |
| the full firmware's switch to setup after `provision erase` on battery (07:06 UTC) | out | spin (no record) |
| BLE started on USB, then USB-C 1 out (the unplug path, setup_sleep.c) | out | works: asleep at 2 s, found, set up on battery |

## Ruled out

- The sleep beginning before BLE started (setup_sleep_begin() while ble_start_task was still in app_clock_init()):
  with setup_boot() waiting for BLE before any sleep, the battery start still spun (2026-11-03 07:06 UTC). Change
  reverted.

## Impact

A board that starts on its cell can never be set up: a new board out of the box, the setup firmware after a lost
presence or a wake on battery (test117, test119 on battery), its 6 h retry (test125).

## Next

On the setup test bed (BLE starts only on `lp_boot`, the console stays up): USB-C 1 out, EN, `lp_boot` - does it spin?
Then compare with USB in what differs at the BLE start: the 32 kHz clock source and the RC32K calibration
(app_clock_init, clock_manager.c), the LDO / PDS settings of setup_boot(), the PGOOD pin's wake configuration.
