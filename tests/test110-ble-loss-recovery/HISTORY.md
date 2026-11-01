# test110: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-01 (run.py, 414 s): pass

29/29 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 456 s): **FAIL**

27/28 checks pass. Failed: phone Bluetooth off before the save: failure told in time (38.3 s: Done. LEAKCAM-B14A joined "VK", reached the internet and stored the ne).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 399 s): pass

29/29 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 428 s): pass

29/29 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 336 s): pass

29/29 checks pass.

- phone USB port was the power source: swapped to sink


### Run 3, 2026-11-01 08:25 (run.py, 647 s): pass 37/37

After the fixes (board: COMMIT attempt id, STATUS kept across connections, store with the phone gone; app: reconnect
and read STATUS for the attempt, connect retried 3x, adapter watched, no "Nothing was stored" guess). Every case as
expected, the Bluetooth case split in two (before the save: by time; during the check: on the app's "is now joining"
log line). BL616 free 122796 B at the end, crash record none before and after.

### Run 2, 2026-11-01 08:15 (run.py, 597 s): fail 33/34

The fixes held in every case; the one failure was the test: the Bluetooth-off delay (6.5 s from the start) hit while
the app was still connecting, so it said "Bluetooth is off" (correct, nothing sent). Fixed in run.py: triggered on the
app's log line.

### Run 1, 2026-11-01 07:46 (run.py, 605 s): fail 31/34

Before the fixes; findings: a link lost with the result left the app saying "Nothing was stored" while the board had
stored the network; the phone's Bluetooth off waited out the 60 s timeout ("did not answer", 66.1 s); a GATT 133 on a
connect was not retried; a link lost with the commit write said "refused the save command".

## Obstacles

- 2026-11-01 13:35, after PR4: "Bluetooth off before the save" came too late: 2 s after the app opened the known board
  was already being checked ("is now joining"), so the app recovered to Done (38.3 s) instead of a named failure. The
  fixed delay replaced by the app's first screen log line (bt_off_at, as the during-check case).

- 2026-11-01: an Android GATT 133 on a connection without any fault armed (the second connection of a save without
  checking) failed case "result notification lost" before its fault: a real phone-side loss, now a finding of its own.
