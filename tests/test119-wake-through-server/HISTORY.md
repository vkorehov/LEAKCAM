# test119: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The server's global `poll` interval is gone: the runner sets the board's check-in interval per device with `PUT
/v1/devices/<id>/intervals` (product default 3600 s, the bench server 60 s). The old line (Setup):

    - The server with the wake endpoint (server/deploy.sh); the runner sets its poll interval to 20 s and puts back 60 s.

## Runs

### Automated run 2026-11-04 (run.py, 550 s): pass

18/18 checks pass.


### Earlier results (README Result before 2026-11-03)

- 2026-11-02 17:52 UTC, the product pair (full + setup, 0.6+g1d2b375-dirty-202611021742), USB-C 1 in, no phone:
  `switch_to_setup wake` (what "ble":1 in the check-in answer does, firmware/bl616_full/presence.c) -> the setup
  firmware's console 5.2 s later (slot 1), awake on USB power, the PIN and the network readable (`provision`) -> **back
  in the full firmware 127.1 s after the command by itself** (the 2-minute timer + the start) -> its check-in reached
  the server (0x03). Open: the real "ble":1 from the server's wake (POST /v1/devices/{id}/wake) and the phone finding
  and setting the board in those 2 minutes (the runner drives the old `provision battery` rule: to rework).

2026-11-01, 12/12 checks (poll 20 s, `provision battery on`): the present board out of range shows Wake (after the first
scan missed it); the tap -> the server 200 -> the board woken at its next check-in 6 s later -> the app shows the
Bluetooth icon and "Change the Wi-Fi" at once; the Wi-Fi changed (Done 8.4 s); a second wake 429; silent again
after the 2-minute window.

### Automated run 2026-11-03 (run.py, 358 s): pass

16/16 checks pass.


### Automated run 2026-11-03 (run.py, 240 s): **FAIL**

12/14 checks pass. Failed: server: a wake after the 2 minutes taken (429); unanswered wake: the setup firmware (not within 60 s: full).


### Automated run 2026-11-01 (run.py, 211 s): pass

12/12 checks pass.

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-03 05:29 UTC, the first run on the product pair (the phone 8 m away in another room): "Wake tapped" failed;
  the phone had been switched to the home screen and Chrome at 05:29:02 (logcat: focus to the launcher, then Chrome),
  so the UI dump at 05:29:12 had no Wake button; the run waited out the next steps' timeouts and was stopped by hand.
  Fixed in run.py: the app brought to the front and the tap retried once (an obstacle line), and the run stops at the
  first failed step (Stop).
- 2026-11-03 05:33 UTC, the second run: the wake worked (the server 200, the board reset into the setup firmware), but
  the runner's `slot` write met the port going away in that reset (OSError EIO in board.py cmd()) and the run crashed.
  Fixed: phone.py firmware() retries on OSError.
- 2026-11-03 05:37 UTC, my own mistake: the runner's log times are local (UTC+2); I took them for UTC, wrote them into
  the docs and set a restart wait two hours late (stopped, the times corrected).
- 2026-11-03 05:41 UTC, the third run: all steps up to the second wake passed (12/12), then "a wake after the 2
  minutes" got 429: the runner counted the 2 minutes from the tap, the server (WakeGap) from the check-in that took
  the wake, 10-15 s later. Fixed in run.py: counted from the board's switch to the setup firmware.
