# test111: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The interval rule by power (the server's `poll` key: 60 s on USB, 24 h on battery) is gone: the server's answer
carries `checkin` (product default 3600 s, the bench server 60 s) and `leakcheck`, set per device with `PUT
/v1/devices/<id>/intervals`; the runner sets the board's interval that way instead of over ssh on the Pi. The old
lines:

    From boot the board checks in at the interval the server gives by its power (`usb=1|0`:
    60 s on USB, 24 h on battery; DESIGN.md 5.3.1) whenever a network is stored; sleeping between check-ins on battery is a
    later use case (the HBN scheduler, test108).

    ... the app's INTERNET permission, ssh to the Pi for the poll interval.

## Runs

### Automated run 2026-11-12 (run.py, 129 s): pass

27/27 checks pass.


### Automated run 2026-11-12 (run.py, 124 s): pass

27/27 checks pass.


### Automated run 2026-11-11 (run.py, 118 s): pass

27/27 checks pass.


### Automated run 2026-11-10 (run.py, 138 s): pass

27/27 checks pass.


### Automated run 2026-11-10 (run.py, 119 s): pass

27/27 checks pass.


### Automated run 2026-11-10 (run.py, 138 s): pass

27/27 checks pass.


### Automated run 2026-11-10 (run.py, 150 s): pass

27/27 checks pass.


### Automated run 2026-11-10 (run.py, 121 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 128 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 118 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 124 s): **FAIL**

26/27 checks pass. Failed: board: last check-in reached the server (0x03) (no answer).


### Automated run 2026-11-09 (run.py, 139 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 131 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 121 s): pass

27/27 checks pass.


### Automated run 2026-11-09 (run.py, 219 s): pass

27/27 checks pass.


### Automated run 2026-11-08 (run.py, 159 s): pass

27/27 checks pass.


### Automated run 2026-11-08 (run.py, 173 s): pass

27/27 checks pass.


### Automated run 2026-11-07 (run.py, 184 s): pass

27/27 checks pass.


### Automated run 2026-11-07 (run.py, 161 s): pass

28/28 checks pass.


### Automated run 2026-11-07 (run.py, 159 s): **FAIL**

27/28 checks pass. Failed: setup Done (30.3 s).


### Automated run 2026-11-07 (run.py, 151 s): pass

28/28 checks pass.


### Automated run 2026-11-07 (run.py, 252 s): **FAIL**

18/27 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); BLE advertisement = the device ID ([]); known board: the short 'change its Wi-Fi' flow (not in the app's log); advertisement: a stored network flagged (?); app: second LEAKCAM deleted (['02CBDF3FD38A', '7C3E8262B149']); server: the delete reached it first, no user left (1); app: the board deleted ({'found': True, 'more': False, 'ms_total': 8630, 't0_epoch_ms': 1794070474393}); no user left: the board back in the setup firmware by itself (full).


### Automated run 2026-11-07 (run.py, 206 s): **FAIL**

23/28 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); BLE advertisement = the device ID ([]); known board: the short 'change its Wi-Fi' flow (not in the app's log); advertisement: a stored network flagged (?).


### Automated run 2026-11-07 (run.py, 223 s): **FAIL**

22/26 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); BLE advertisement = the device ID ([]); advertisement: a stored network flagged (?).


### Automated run 2026-11-07 (run.py, 238 s): **FAIL**

23/28 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); BLE advertisement = the device ID ([]); known board: the short 'change its Wi-Fi' flow (not in the app's log); advertisement: a stored network flagged (?).


### Automated run 2026-11-07 (run.py, 240 s): pass

28/28 checks pass.


### Automated run 2026-11-07 (run.py, 243 s): **FAIL**

27/28 checks pass. Failed: known board: the short 'change its Wi-Fi' flow (Your phone now asks to pair with 7C3E826 | The LEAKCAM is now joining "<network>" to check | Done. 7C3E8262B149 joined "<network>", reached ).


### Automated run 2026-11-07 (run.py, 291 s): **FAIL**

23/26 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); app: second LEAKCAM offline when it stopped (02A1DB719ADD: online, Wi-Fi "t111-second").


### Automated run 2026-11-07 (run.py, 304 s): **FAIL**

24/26 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x).


### Automated run 2026-11-07 (run.py, 216 s): **FAIL**

23/26 checks pass. Failed: setup Done (0.0 s); board: last check-in reached the server (0x03) (no answer); app: second LEAKCAM listed, not seen yet (02AB12CD34EF: offline, last seen 4 min ago, Wi-Fi "t111-second").


### Automated run 2026-11-07 (run.py, 184 s): **FAIL**

26/28 checks pass. Failed: known board: the short 'change its Wi-Fi' flow (Your phone now asks to pair with 7C3E8262B149.

• ); app: second LEAKCAM offline when it stopped (02AB12CD34EF: online, Wi-Fi "t111-second").


### Automated run 2026-11-06 (run.py, 154 s): pass

28/28 checks pass.


### Automated run 2026-11-04 (run.py, 324 s): **FAIL**

24/25 checks pass. Failed: app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-04 (run.py, 203 s): **FAIL**

18/25 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); known board: the short 'change its Wi-Fi' flow; board: last check-in reached the server (0x03) (no answer); app: the board online throughout (7C3E8262B149: offline, last seen 57 s ago, Wi-Fi "<network>"); app: the board still listed after the delete (7C3E8262B149: offline, last seen 1 min ago, Wi-Fi "<network>"); app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-04 (run.py, 354 s): **FAIL**

20/25 checks pass. Failed: setup Done (0.0 s); pairing: the app entered the PIN, no PIN prompt on the phone (dialog started 0x); known board: the short 'change its Wi-Fi' flow; board: last check-in reached the server (0x03) ([0m[36mbouffalolab />[0m); app: the deleted board back in the list by itself (USB, server online) (75 s, ).


### Automated run 2026-11-01 (run.py, 187 s): pass

25/25 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 135 s): pass

24/24 checks pass.

- phone USB port was the power source: swapped to sink


### Run with the status screen 2026-11-01 11:24 (run.py): pass 18/18

After four runs that failed on the test driver and one app bug (below), all checks pass.

### Automated run 2026-11-01 (run.py, 116 s): pass

18/18 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 102 s): pass

18/18 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 115 s): **FAIL**

17/18 checks pass. Failed: app: the board listed and online.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 144 s): **FAIL**

15/16 checks pass. Failed: setup Done (0.0 s).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 128 s): pass

12/12 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 09:52 (run.py, 137 s): pass

12/12 checks pass (first run).

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-01, status screen: the driver's scrolling find() lost the enabled "set up" button for 40 s while the list
  redrew (now UiAutomator's own wait for it); the phone moved the app to the background a second after pairing (now
  brought back to the front); a second Find after the status screen missed the board once (no longer tapped); a fixed
  6 s wait missed a slow first presence refresh (now polled). App bug found by it: the status screen opened the setup
  with the board listed but not picked, Send said "pick your board" (fixed: openSetup picks it).

- 2026-11-01: a console command typed right after the board started checking in came back "command not found" (the Wi-Fi
  stack's log lines interleaved with it); run.py retries the `presence` read once. The same log flood cut test106's
  product detection short (it read `help`): phone.product() now asks `prov` itself.

## Obstacle 2026-11-10 09:52 UTC+2: the harness crashed on a vanished port (fixed)

Right after the setup's Done the board switched to the full firmware; `presence now` was written to the BL616 port as
it disappeared (OSError ENODEV) and the run stopped with a traceback. board.bl() now asks again once the port is back
(one retry), as it already waits for a missing port.
