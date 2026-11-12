# test118: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The server's global `poll` interval is gone: the runner sets the board's check-in interval per device with `PUT
/v1/devices/<id>/intervals` (product default 3600 s, the bench server 60 s). The old line (Setup):

      board would), poll interval 10 s for the run.

## Runs

### Automated run 2026-11-12 (run.py, 209 s): pass

6/6 checks pass.


### Automated run 2026-11-12 (run.py, 497 s): **FAIL**

0/7 checks pass. Failed: start: Wi-Fi and Bluetooth icons, not red (not within 60 s, row None); BLE gone: the Bluetooth icon goes (not within 120 s, row None); BLE gone: the Wi-Fi icon stays, not red (None); presence gone: the Wi-Fi icon goes, the row red (not within 70 s, row None); presence back: the Wi-Fi icon again, not red (not within 40 s, row None); BLE back: the Bluetooth icon again (not within 60 s, row None); both gone: no icon, the row red (not within 120 s, row None).


### Automated run 2026-11-12 (run.py, 3 s): **FAIL**

not run: no pt_ble.


### Automated run 2026-11-07 (run.py, 2 s): **FAIL**

not run: no pt_ble.


### Automated run 2026-11-01 (run.py, 217 s): pass

7/7 checks pass.

- phone USB port was the power source: swapped to sink


## Obstacles

- the BL616 test firmware with pt_ble is needed (tests/firmware/bl616_powertest) (2026-11-12, automated run)

- the BL616 test firmware with pt_ble is needed (tests/firmware/bl616_powertest) (2026-11-07, automated run)
