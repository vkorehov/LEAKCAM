# test050-clock-without-ntp: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-07 (run.py, 271 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 271 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 321 s): **FAIL**

8/11 checks pass. Failed: the BL616 30 s ahead: the K230 steps to its next frame (logged about -30 s off) (no clock line); the BL616 30 s off: the next check-in's X-Time sets it back (within 2 s of UTC) (check-in 58 s later, set 2 -> 2, off -1.6 s); the K230 follows back (about +30 s off before) (no clock line).


### Automated run 2026-11-07 (run.py, 168 s): **FAIL**

7/8 checks pass. Failed: the K230 30 s behind: the next BL616 frame sets it (logged), within 2 s of UTC (no clock line, off -31 s).


## Obstacles

- **2026-11-07, the K230 shell's `date` is not the program's clock:** set there 30 s behind, `date` read -31 s but the
  running program saw no difference and stepped nothing (step 6 FAIL). The rule is driven from the BL616's clock
  instead (`clock <unix s>`).
- **2026-11-07, runner timing:** the K230's `clock:` line reached the bench more than 1 s after the BL616's DONE (read
  3 s now); the bench's 60 s check-in landed between steps 6 and 7 and set the BL616 back early (step 7 moves it again
  right before its wait).
