# test148-video-worst-case: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-13 (run.py, 94 s): pass

12/12 checks pass.


### Automated run 2026-11-13 (run.py, 2 s): **FAIL**

not run: the board is not in the full firmware


### Automated run 2026-11-13 (run.py, 69 s): **FAIL**

3/6 checks pass. Failed: none: the K230 uploaded 2 videos, failed 0 (0 uploaded, 2 failed); none: v1 (J4) at the server (no MP4); none: v2 (J5) at the server (no MP4).


### Automated run 2026-11-13 (run.py, 111 s): **FAIL**

18/19 checks pass. Failed: frame: v1 (J4) bit rate held: average <= 11000, peak second <= 15000 kbit/s (average 9183, peak 15657 kbit/s).


### Automated run 2026-11-13 (run.py, 108 s): pass

19/19 checks pass.


### Automated run 2026-11-13 (run.py, 196 s): pass

37/37 checks pass.


### Automated run 2026-11-13 (run.py, 202 s): pass

37/37 checks pass.


### Automated run 2026-11-13 (run.py, 63 s): pass

10/10 checks pass.


### Automated run 2026-11-13 (run.py, 63 s): pass

10/10 checks pass.


### Automated run 2026-11-13 (run.py, 81 s): **FAIL**

9/10 checks pass. Failed: 3 check-ins after the videos: all reach the server (0x03) (0x03 0x03 0x12).


### Automated run 2026-11-13 (run.py, 93 s): **FAIL**

9/10 checks pass. Failed: 3 check-ins after the videos: all reach the server (0x03) (0x15 0x15 0x12).


### Automated run 2026-11-13 (run.py, 64 s): **FAIL**

9/10 checks pass. Failed: 3 check-ins after the videos: all reach the server (0x03) (0x03 0x15 0x15).


### Automated run 2026-11-13 (run.py, 120 s): **FAIL**

27/28 checks pass. Failed: 3 check-ins after the videos: all reach the server (0x03) (0x12 0x03 0x03).


### Automated run 2026-11-13 (run.py, 238 s): **FAIL**

20/29 checks pass. Failed: none: the K230 uploaded 2 videos, failed 0 (6 uploaded, 0 failed); frame: v1 (J4) every frame decodes, 225 frames at 15 fps (225 frames, 14.49 fps); frame: v1 (J4) bit rate held: average <= 11000, peak second <= 15000 kbit/s (average 9581, peak 15738 kbit/s); step: SENT within 90 s of the check-in (none (7C3E8262B149-1794545803)); step: the K230 uploaded 2 videos, failed 0 (0 uploaded, 2 failed); step: v1 (J4) at the server (no MP4); step: v2 (J5) at the server (no MP4); random: v1 (J4) bit rate held: average <= 11000, peak second <= 15000 kbit/s (average 10471, peak 15371 kbit/s); 3 check-ins after the videos: all reach the server (0x03) (0x15 0x15 0x12).


## Obstacles

- 2026-11-13 04:51 UTC: the board was in the setup firmware (the PC, the server, had been off: 3 failed check-ins);
  `slot switch` back.
- run.py's first start failed in harness.Run: the README needs the Metrics table (copied from test147).
- The first run's "uploaded 2" check read an older session's `media: done` line still in the console buffer; the
  checks now read the newest `media: video <base>` segment.
- The console's `pwm_set` lines interleave with the app's lines (one UART): harmless for the regexes used.
- Seen once, not explained: a K230 STATUS-like fragment ("...HIGH10,6575168,6340992,38064128,") arrived at the BL616
  shell as a command, and "68" at the K230's msh after a boot ("68: command not found"): link text reaching the
  other side's shell.
- `pgrep -f` in my own wait loop matched itself (memory never-kill-by-pattern): waited on the log instead.
