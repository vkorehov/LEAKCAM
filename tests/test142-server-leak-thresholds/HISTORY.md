# test142: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-12 (run.py, 64 s): pass

13/13 checks pass.


### Automated run 2026-11-12 (run.py, 62 s): pass

13/13 checks pass.


### Automated run 2026-11-11 (run.py, 66 s): pass

13/13 checks pass.


### Automated run 2026-11-10 (run.py, 75 s): pass

13/13 checks pass.


### Automated run 2026-11-10 (run.py, 61 s): pass

13/13 checks pass.


### 2026-11-10: written

The user asked for the probe voltage and the humidity threshold from the server, stored in flash, as attributes of
the presence (check-in) answer, written only when different from the stored ones. Built: thresholds.c (both
firmwares), the server's PUT + thresholds.json, `thmv`/`thrh`, leakIn per device (`go test` TestThresholds pass,
deployed). First draft caught console lines of a check-in; replaced by state (`thresholds` counts its flash writes,
`leak` shows the armed reference): a console line can be missed, a state read cannot.

## Obstacles

- The board was in the setup firmware with no network stored and the bench phone at 2 % (no charger on Wi-Fi adb):
  the run waits for the phone (only it stores a network).

### 2026-11-10 09:24 UTC+2: first run, FAIL 12/14 (test-side, fixed)

Steps 1-6 pass on the board: defaults reported, 900 mV / 70 %RH taken 4.8 s after the PUT (triggered check-in), one
flash write, the comparator re-armed at 900 mV, no write for the same values at two more check-ins, kept through a
reset with the server down, 75 %RH alarmed at 70 and the server's leak in 1.7 s, the setup firmware on the same values.
"back to the full firmware" failed: `switch_to_full usb` only reports the setup firmware's check state; the way back
after `switch_to_setup lost` is a reset (a cable start checks the stored network and switches, as test128). Step 7
failed after it (the board still in setup). Fixed: reboot, then wait for the full firmware.
