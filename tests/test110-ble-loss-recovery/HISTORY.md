# test110: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-04 (run.py, 518 s): pass

33/33 checks pass.


### Earlier result (README Result before 2026-11-03)

run.py, 2026-11-01 08:25 (run 3), BL616 test firmware with pt_fault + the setup app with reconciliation, bench phone
(Samsung, Android 13): 37/37, 647 s; crash record none before and after; BL616 free 122796 B at the end.

| Case | App, time | Board |
|---|---|---|
| every notification lost (5) / every second (2) / the result's (1) | Done 7.3 / 7.1 / 7.4 s | real network |
| link lost at every connect (6 faults = 3 connects) | "disconnected (133)", 5.7 s | decoy |
| link lost when the check starts / getting an address / reaching the internet / with the result | Done 9.5 / 10.0 / 10.2 / 10.3 s (reconnect + STATUS read) | real network |
| DNS / HTTP answer lost | named, 7.2 / 7.3 s | decoy |
| phone Bluetooth off 4 s before the save | "Bluetooth is off", 11.1 s | decoy |
| phone Bluetooth off 4 s during the check | Done 14.6 s (reconnected when it came back) | real network |

Every clean setup after a case: Done, 7.1-7.6 s. The fixes that got here (run 1: 31/34) are in HISTORY.md.

### Automated run 2026-11-03 (run.py, 423 s): pass

30/30 checks pass.


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
