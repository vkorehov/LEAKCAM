# test102: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The session the 2026-10-30 figures integrated was the first product's agent session (test101's old timeline:
`leakcam_agent` 24.5 s after READY with no camera and no server). The product program has no agent: a session on
battery is READY -> WAKE -> the job -> `DONE` -> rails off, one leak check a day plus one per wet probe, and the
budget is 24 check-ins + 1 leak check a day (~2.85 mAh/day estimate). The old lines:

- Steps: "`pt_on` held in reset, booting, agent idle, KPU loop, encoder (test027), LEDs at 100 % (white, IR)."
- Current: "session, agent working: 228 mA (new reading)" - the meter reading itself stands as the K230 running with
  no camera and no server (2026-10-30).
- Power, energy per operation: "one wake (test101 timeline: boot 0.87 s at ~87 mA, agent 24.5 s at 228 mA, 2.3 s at
  220 mA): 6.17 A*s = 1.71 mAh at the board, 30.8 J from USB (2026-10-30)".
- Per day: "4 report sessions a day (6 h, DESIGN 5.1): 6.9 mAh/day at the board (2026-10-30)".

## Runs

### Automated run 2026-11-12 (run.py, 12 s): **FAIL**

no meter reading given.


### Automated run 2026-11-12 (run.py, 3 s): **FAIL**

not run: no pt_boot, pt_session on the BL616.


### Automated run 2026-11-07 (run.py, 10 s): **FAIL**

no meter reading given.


### Automated run 2026-10-30 (run.py, 9 s): pass

session current read on USB: 228 mA while the agent works

- meter K230 session, the agent working (no camera, no server): 0.228 A (read in test102-session-current)


### Automated run 2026-10-30 (run.py, 218 s): pass

session current read on USB: 228 mA while the agent works

- meter K230 session, the agent working (no camera, no server): 0.228 A


Not run yet.

### Superseded statements

- Setup (2026-10-21): **Check the mA fuse** of a borrowed meter: one with a burned fuse read nothing useful; shorting its leads "fixed"
  the board. Readings from before the swap are void.
- Steps: 1. Rails off (BL616 only): sleep-level current (the BL616 test firmware does not hibernate: product firmware later).

## Obstacles

- **2026-11-12 04:17, FAIL in the all-tests batch: not a regression.** The test is deferred (user 2026-11-08); its
  run.py is the old test firmware's (`pt_session`) and records only meter readings, none asked unattended. To be
  rewritten for the product pair and read with the cell out or full.

- the BL616 firmware has no pt_boot, pt_session: flash the test firmware that has it (2026-11-12, automated run)

None.
