# bug-007: no advertising after a phone leaves during the setup firmware's sleep

Found: 2026-11-04, the bug-006 follow-up cases (bugs/bug-006/cases.py). Status: fixed (the sleep back 1 s after a
disconnect); cases `gone` and `connects` pass on the setup test bed (2026-11-04 17:09 UTC, 13/13).

## What happened

In a sleep window (`wakeup_timer <ms> usb`, the setup firmware's PDS sleep between advertisements), after any phone
connection ended the board was never found or reached again in that window and after it, until a reboot - while the
host said "advertising" (`lp_sleepstat`: host advertising 1, adv_on 1, no session):

| Case | Before the fix | After |
|---|---|---|
| `gone`: the phone's Bluetooth off mid-session | not found in 60 s; 32-40 sleeps in 60 s, one of 58.9 s | found again; 439 sleeps |
| `connects`: 6 sessions in one 150 s window | 1/6 (2-6: "no connection" after 5 s); 33 sleeps | 6/6; 1150 sleeps |
| the same sessions with the board awake (no window) | 4/4 | - |

On battery the setup firmware is always in that sleep: one failed or cut-off setup (the phone walks away, a wrong
password retried later) left a board nobody could set up until the next switch to the full firmware. test116 passed
only because each of its setups worked at the first connection.

## Cause

At a disconnect the sleep came back (tickless on) at once; the SDK's sleep path (tickless_accurate.c) then asked the
controller for its next deadline (btble_controller_sleep) right after the advertising restart. The controller had
none (0) and was parked for the rest of the window, its advertising never scheduled again. The controller's own
reason is not visible (closed library); what was measured:

- restart advertising first, the sleep after it (same callback, at once): still 1/6;
- refuse the sleep while the controller answers "no deadline" (a wrap of btble_controller_sleep returning -1): the
  controller kept answering 0 while asked at every idle, the chip never slept and the watchdog (fed only while awake
  outside a window) reset it after 19 s (crash #1, "watchdog hang in task 'IDLE'") - asking itself is the harm;
- the sleep back 1 s after the restart: 6/6, and the gone case passes.

## Fix

setup_sleep.c: a connection keeps the chip awake (ble_pairing_link_cb(true)); when the phone has left and
advertising was restarted (ble_pairing_link_cb(false), after adv_start() in on_disconnected) a 1 s one-shot timer
turns the sleep back on; a new connection within that second stops it. Firmware: setup firmware + test bed,
2026-11-04.

## Workaround before the fix

None on battery: USB-C 1 in (the board stays awake) or a reboot.
