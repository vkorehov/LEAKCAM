# test116: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-09 (run.py, 123 s): pass

16/16 checks pass.


### Automated run 2026-11-09 (run.py, 256 s): **FAIL**

1/2 checks pass. Failed: no network: the setup firmware (setup). Stopped at: no network: the setup firmware.


### Automated run 2026-11-09 (run.py, 257 s): pass

16/16 checks pass.


### Automated run 2026-11-08 (run.py, 80 s): **FAIL**

2/3 checks pass. Failed: unplugged: the phone finds the board asleep (not within 60 s). Stopped at: unplugged: the phone finds the board asleep.


### Automated run 2026-11-08 (run.py, 143 s): **FAIL**

2/3 checks pass. Failed: unplugged: the phone finds the board asleep (not within 60 s). Stopped at: unplugged: the phone finds the board asleep.


### Step 5 by hand 2026-11-08 07:39 UTC+2 (USB-C 1 back in since 07:36)

The phone helpers' checks of run.py step 5: the full firmware, silent (15 s scan, nothing found), present at the
server. With the run of 23:31, every step of test116 passes on the new app (14/14).

### Automated run 2026-11-07 23:31 UTC+2 (run.py --unplugged, 1084 s): stopped, not a pass

11/11 checks pass, steps 2-4 (found asleep 12.7 s after the start, set up on battery 35.7 s, full firmware 16 s after
Done, silent 30 s; erased on battery: setup firmware found 40.6 s after the erase, set up again 26.1 s, full 7 s after
Done, present). Step 5 (USB-C 1 back in) never ran: no hand step within 15 min. run.py logged this as a pass: fixed,
a stop now fails the run. App setup from "Connecting" to Done 17.2 s, over 15 s: two GATT connects to the sleeping
board timed out (status 133, 5 s each), the third connected in 2 s, pairing + Wi-Fi check 3.8 s. Accepted as the exception to the 15 s rule
(user, 2026-11-08; DESIGN.md 5.3.4).


### Automated run 2026-11-07 (run.py, 162 s): **FAIL**

3/4 checks pass. Failed: unplugged: set up from the app on battery (PIN, no dialog) (20.1 s, pairing ). Cause: the
board was still on this phone's list, and Setup skips known boards (DESIGN.md 5.3.4); run.py now calls only_boards()
before each setup.


### Automated run 2026-11-03 (run.py, 216 s): pass

16/16 checks pass.


### Automated run 2026-11-03 (run.py, 231 s): **FAIL**

8/9 checks pass. Failed: battery start: the phone finds the setup firmware (not within 120 s).


### Automated run 2026-11-03 (run.py, 195 s): **FAIL**

6/7 checks pass. Failed: battery start: the phone finds the setup firmware (not within 120 s).


### Automated run 2026-11-03 (run.py, 4 s): **FAIL**

not run: the phone is locked: unlock it.


### Automated run 2026-11-03 (run.py, 119 s): **FAIL**

2/3 checks pass. Failed: on battery: the phone finds the board (None rssi None, 32.1 s after the reset).


## Obstacles

- 2026-11-03 20:21 UTC, the third run (the sleep veto fixed): step 4 spun again (crash #15, watchdog hang in
  ble_start at 6.0 s); an EN tap with USB out -> the setup firmware started on the cell and was found at once: the
  core had been left wedged by the run before (its sleep on battery, the switch's reset), not the battery start.
  Fixed by a PDS reset at the setup firmware's start (bugs/bug-006); the 20:28 run passed.
- 2026-11-03 20:15 UTC: the phone off the Wi-Fi (asleep), adb's port changed to 46003 (user); ~/.leakcam_phone updated.
- 2026-11-03 20:27 UTC: the VM's xHCI hung at the plug (err -32), rebound (0000:0a:00.0).

- 2026-11-03 06:30 UTC, the first run (the board reset over PR1 after the unplug, to start as from its cell): never
  found, even with the phone next to the board: the BLE start spun in rwip_time_get (crash #134); a ROM power-on
  reset did not clear it, EN did (user, BOOT + EN). bugs/bug-006.
- 2026-11-03, reading the code for the run: the setup firmware stayed awake at 100 ms advertising after an unplug
  (only the plug had a path). Fixed in setup_sleep.c (the unplug read every second while awake); proven in the
  07:04 UTC run.
- 2026-11-03 07:01 UTC: the runner stopped at its start (the phone locked) after the user had already unplugged:
  `--unplugged` added (no console while the setup firmware sleeps).
- 2026-11-03 07:08 UTC, the second run: steps 2-3 pass (found 3.6 s, Done 14.8 s on battery, present, silent); step 4,
  the setup firmware started on battery, spun in the BLE start again, also with the sleep held off until BLE had
  started (that change reverted). bugs/bug-006.

- the phone is locked: unlock it (screen lock off for the bench phone, or Developer options > Stay awake) (2026-11-03, automated run)
