# bug-006: the setup firmware's BLE never starts with USB-C 1 out

Found: 2026-11-03, test116 (the whole setup over BLE alone). Status: fixed (two causes, two fixes); test116 passes on battery
(2026-11-03 20:28 UTC, 16/16).

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

## Cause

A PDS sleep before BLE had started. On battery the setup firmware begins its sleep (setup_sleep_begin()) right after
setup_boot() has created `ble_start`; when that task yields in app_clock_init() (flash, the console) the idle hook
ran, `ble_pairing_sleep_ok()` answered true for "BLE not started yet" (`!started ||`), and the chip went into PDS
under the controller's start: the BLE core wedged as in test120 (only EN clears it; the SDK's PDS reset that would
clear it at BLE start is the empty hook that keeps USB alive). On USB the setup firmware never sleeps: never seen
there.

Reproduced without hands on USB (setup test bed, 2026-11-03 ~20:03 UTC): a reset, then `lp_boot` and at once
`wakeup_timer 5000 usb` (the shell task outruns the start): `ble_start` spinning, `lp_sleepstat` "sleeps entered 1,
BLE idle vetoes 0". The reset of the slot switch inside a sleep window with 2 s advertising (`lp_reset_in`, 3 offsets)
did not wedge it.

## Fix

1. `ble_pairing_sleep_ok()` = advertising or connected: no PDS before BLE runs, whoever begins the sleep (the start,
   the unplug path, a wake). The race above on the fixed test bed: BLE up 3/3, "BLE idle vetoes" ~5800 during the
   start, then sleeps (46-47 in the window).
2. The second cause (found with fix 1 in, 20:21 UTC): a setup firmware that had slept on battery and switched to the
   full firmware left the core wedged; the next battery start spun. An EN tap with USB out: the setup firmware then
   started on the cell and was found at once - the start on battery is fine, the core was left asleep by the run
   before (its reset during the battery sleep; the same reset on USB, `lp_reset_in` 3/3, did not wedge it). Fix: the
   PDS block reset the controller's init would do, at the very start of the setup firmware's main(), before
   board_init() brings USB up (done at BLE start it killed the USB device, 2026-10-29): it clears a core left asleep
   by any reset - a switch, the watchdog, a brown-out. USB and BLE up after it on USB (found in 0.15 s), and test116
   on battery 16/16.

## Ruled out

- The reset inside a sleep window (the BLE core in its own deep sleep at the slot switch's POR reset): 3/3 fine.
- setup_boot() waiting for BLE before returning (07:06 UTC run): the battery start still spun, as another path could
  begin the sleep before BLE ran (the new unplug path in usb_plug_task reads PGOOD from the start); the veto covers
  every path. Reverted.

## Impact

A board that starts on its cell can never be set up: a new board out of the box, the setup firmware after a lost
presence or a wake on battery (test117, test119 on battery), its 6 h retry (test125).

## Follow-up (2026-11-03, user: add the cases)

The veto reads ble_pairing.c's own flags (adv_on, active_conn), not the host's state. Where BLE can be idle after its
start, and what to do:

- No pairing PIN stored: on_bt_ready() never advertises -> vetoed forever -> awake on battery (~50 mA) until the cell
  is flat. Change: no PIN -> the BLE controller not started at all, the sleep allowed ("never started" is safe, "not
  started yet" was the wedge) - one experiment first: a sleep window with BLE never started, then `lp_boot`.
- A stale adv_on (on_connected() returning early, the controller stopping advertising unseen): the veto says
  "advertising" while BLE is idle -> the wedge, and no advertising again. Change: the veto from the host's own state
  (bt_dev.flags BT_DEV_ADVERTISING, an LE connection looked up), not the module's flags.

Cases to add (setup test bed, the `wakeup_timer <ms> usb` sleep window, no hands), each ending in a reset + `lp_boot`
(BLE up = no wedge left):
1. The phone gone while asleep: a session, the phone's Bluetooth off mid-connection -> advertising again, sleeps go
   on, found again. Then on battery (test110 `--battery`).
2. No PIN: sleeps (after the change) and BLE stays healthy.
3. Many connects / disconnects in one sleep window: vetoes vs sleeps counted, no wedge accumulated.
