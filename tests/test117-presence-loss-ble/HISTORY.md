# test117: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Earlier results (README Result before 2026-11-03)

- 2026-11-02 17:55 UTC, the product pair (full + setup, 0.6+g1d2b375-dirty-202611021742; the full firmware without BLE),
  USB-C 1 in, no phone: the server stopped -> the full firmware's next check-in failed -> **the setup firmware 36.3 s
  later** (slot 1); on USB power it stayed (only the 6 h retry pending, firmware/bl616_setup/switch_to_full.c); the
  server back and a `slot switch` by hand (standing in for the retry) -> the full firmware checked in again (0x03).
  Open: the phone finding the board in the setup firmware and setting the Wi-Fi out of it (the runner still drives the
  old `provision battery` rule: to rework), the battery case (setup sleeping, the 6 h retry).

2026-11-01, 13/13 checks (poll 10 s, `provision battery on`): present -> the phone does not find the board; the server
stopped -> presence lost in 5 s, the phone finds the board (its serial) in 0.3 s; the server back -> silent again in
9 s; a missing network saved without checking -> lost in 4 s; the app sets the real network at once (Done 8.7 s) ->
silent again 2 s later, present at the server 14 s after that setup started.

### Automated run 2026-11-03 (run.py, 189 s): pass

19/19 checks pass.


### Automated run 2026-11-01 (run.py, 134 s): pass

13/13 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 163 s): **FAIL**

10/13 checks pass. Failed: network gone: the phone sets the real network at once (0.0 s); network back: advertising off again (not within 30 s: on (presence lost)); network back: present at the server (60 s after the setup started).

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-01 14:38, first run: "server down: advertising (presence lost)" not within 40 s; the board's check-in that
  failed came at 14:38:20, 1 s after the wait: it was still on the old 60 s interval. The check-in right after the save
  (presence_now) had come while the phone was still connected and was skipped until the next whole interval. Fixed in
  presence.c (a skipped check-in tries again in 2 s); the runner waits until the board took the run's interval
  (`presence`: poll). The battery rule itself switched ("on (presence lost)" right after that check-in). The runner was
  stopped by its PID: its finally did not run, the poll interval and `prov battery` were put back by hand.

- 2026-11-01 14:42, second run: the server-down case passed (lost in 4 s, found in 0.2 s, off again in 7 s); the
  setup right after the missing network failed at once (BleOp: NullPointerException): the reopened app offered the
  board **Wake** (online at the server, not yet found by the first scan) and BleOp tapped the first enabled setup
  button. Fixed in the app (Wake only after a status scan missed the board) and in BleOp (never taps Wake).
