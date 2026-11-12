# test132: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-12 (run.py, 974 s): **FAIL**

stopped: the probe never wet


### Automated run 2026-11-08 (run.py, 160 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 265 s): **FAIL**

9/11 checks pass. Failed: battery, the RTC's check: a K230 session rtc (none); wet probe: `leak: probe wet` and a K230 session leak ().


### Automated run 2026-11-07 (run.py, 542 s): **FAIL**

8/11 checks pass. Failed: unplugged: the server sees chg 0 at once, the battery read (0 s, bat None mV); wet probe: `leak: probe wet` and a K230 session leak (); wet probe: DONE, the K230 off after it (None, rails on None ms).


## Obstacles

- **2026-11-12 02:45, CHECK FAIL "ks 0, klast 1" (test side).** ks counts K230 sessions since the last good check-in
  and is 0 again in the next one; the check read a later check-in than the one that carried the job. Now: klast 1 and
  (ks >= 1 or kago <= 60 s), from the check-in that showed the job. The same run then waited 15 min for the hand step
  (wet the probe) at night: rerun with someone at the bench.
