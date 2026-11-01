# test114: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-01 (run.py, 541 s): **FAIL**

Advertising interval -> phone discovery: 100 ms: 10/10 found, median 224.5 ms, max 549 ms; 500 ms: 10/10 found, median 553.5 ms, max 1814 ms; 1000 ms: 10/10 found, median 756.5 ms, max 2541 ms; 2000 ms: 10/10 found, median 2182.5 ms, max 4085 ms; 5000 ms: 9/10 found, median 3156 ms, max 9539 ms; 10240 ms: 8/10 found, median 8423.0 ms, max 22419 ms.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 0 s): **FAIL**

not run: no pt_ble.


## Obstacles

- 2026-11-01 13:36, not run: run_tests.py rebooted the BL616 (test firmware) after test110 and its USB never came back
  (last disconnect 13:36:03, no enumeration attempt in the VM after that); a USB-C 1 replug and a reset press gave
  nothing in the VM, an xHCI rebind neither (the phone came back each time); a second replug brought it back. Crash
  record: none. Unexplained: a hang without a record, or the device stuck between the Mac and the VM (no Mac-side
  view from here). Watched on the next batch reboots.

- the BL616 test firmware with pt_ble is needed (tests/firmware/bl616_powertest) (2026-11-01, automated run)
