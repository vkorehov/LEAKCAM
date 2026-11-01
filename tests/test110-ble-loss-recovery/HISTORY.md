# test110: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-01 07:46 (run.py, 605 s): fail 31/34

First run; findings in README Result (consistency after a link lost with the result, phone Bluetooth off, GATT 133 not
retried).

### Automated run 2026-11-01 (run.py, 605 s): **FAIL**

29/32 checks pass. Failed: the result notification lost: decoy stored first; link lost with the result: app and board agree (app not stored, board real network); phone Bluetooth off during the check: failure told in time (66.1 s: The LEAKCAM did not answer. Check it is still plugged into USB power a).

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-01: an Android GATT 133 on a connection without any fault armed (the second connection of a save without
  checking) failed case "result notification lost" before its fault: a real phone-side loss, now a finding of its own.
