# test108: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded plan (2026-10-30, before DESIGN.md 5.3.12)

The first plan of this test (never run): the BL616's own production schedule in HBN RAM (humidity every 10 min, the
K230's report interval, the server's "poll"), the remembered AP and DHCP lease reuse, the server address from the
agent, a stream request, back-off. Humidity jobs and "poll" are gone (2026-11-06/07); the lease reuse, the remembered
AP and the back-off come with the HBN sleep step (test132). The old README, whole:

    # test108: production server check by the BL616 (schedule, lease reuse, server address, back-off)
    
    Status: **planned** (2026-10-30): the scheduler, lease reuse and server address are not written yet (DESIGN 5.1
    Production design): they go into firmware/bl616_full and the test firmware, growing test100's `pt_poll`.
    
    The BL616 alone asks the server whether a viewer wants live video (`POST /v1/poll`, the metrics in its form: DESIGN.md 5.3.10), without the K230. Production
    design: jobs scheduled by time in HBN RAM (humidity 10 min, the K230's report interval, the server's poll interval);
    join at the remembered AP; the DHCP lease reused while valid; the server's IP cached; the server address comes from
    the agent (`SERVER,<host>,<port>`, stored in easyflash when it changed); a stream request starts a K230 session with
    reason `stream`; failures back off; a check never delays a leak or humidity wake.
    
    ## Setup
    
    - The server on the Windows PC (192.168.1.131): the mock with `/v1/poll` (to add) and a fault mode (down, slow, HTTP
      500, wrong answer), a `poll` interval it can change, a stream request on demand.
    - The bench AP from `~/.leakcam_wifi`; a way to take the AP off (router Wi-Fi off) and to break its internet.
    - Battery (cell full); the USB-C 1 meter for the awake current; the BL616 console on PR1.
    - Short intervals for the run (poll 60 s, report 5 min), the product's in the energy step.
    
    ## Needs (build features and dependencies)
    
    BL616: RTC scheduler in HBN RAM, Wi-Fi with the remembered AP and lease reuse, its lwIP (6 KB heap), easyflash for
    the server address, the K230 power path; K230: agent `SERVER` message, `leakcam_stream` for a stream request.
    
    (What this test uses; the union over all tests is the feature set the minimal image keeps.)
    
    ## Steps
    
    1. Schedule: 2 h with all three jobs at short intervals: every job ran at its tick (+- one RTC slice), jobs due
       together ran in one wake, no wake without a due job (evlog).
    2. Least radio: the first check joins + DHCP; the next ones reuse the lease (no DHCP on the air) and the cached server
       IP; a lease past its time, a changed AP or a new server address does DHCP / DNS again. Awake time per check.
    3. Server interval: `"poll":<s>` in an answer is used from the next check and kept over HBN.
    4. Stream request: `{"stream":30}` -> K230 session with reason `stream`, both cameras streamed 30 s, then sleep.
    5. Server address: the agent sends `SERVER` each session; the BL616 writes easyflash only when it changed (count the
       writes over 20 sessions); a new address is used at the next check.
    6. Failures: AP off, AP without internet, server down, HTTP 500, a stalled answer: the first failed check-in switches
       to the setup firmware at once (DESIGN.md 5.3.1; lease and cached IP dropped), the 6 h retry brings it back
       (test125); nothing hangs the BL616 (watchdog not fired).
    7. Priority: a leak edge and a humidity alarm during a check, and during a back-off: the K230 session starts at once,
       the check is dropped or finished after.
    8. Nothing configured: no credentials, or no server address: no check; the board runs the setup firmware (BLE in low
       power, test120).
    9. Energy: awake time and mAh per check at the product interval, per day; target 0.6-0.8 s awake (DESIGN).
    
    ## Pass
    
    Every check runs on its schedule with the least radio time; a stream request becomes a session; every failure backs
    off and recovers; a leak or humidity wake is never delayed by a check; no configuration means no radio.
    
    ## Result
    
    Not run yet.
    
    ## Metrics (**MISSING** on every run: skill leakcam-test-metrics)
    
    Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).
    
    | Metric | Value | Top / budget | Note |
    |---|---|---|---|
    | Boot, power-on -> READY (median / max of 5) | **MISSING** | target (test022) | longest step |
    | This part's boot share | **MISSING** | - | driver / mount / probe |
    | Test run time | **MISSING** | - | setup separately |
    | Speed / rate against the top | **MISSING** | SPEEDS.txt | % of the top |
    | Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |
    | Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | probe points named |
    | Power, energy per operation | **MISSING** | - |  |
    | Per day | **MISSING** | ENERGY.txt line | n per day |
    | Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
    | Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
    | Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
    | Compile flags | **MISSING** | smallest code (rule) |  |
    | Features over the minimal image | **MISSING** | test008 floor | bytes, boot ms, RAM, current |
    | Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |
    
    ## Boot time
    
    Wake -> radio up -> association at the remembered AP -> (lease reused or DHCP) -> answer -> HBN, per step (test100
    pt_poll 1.71 s on the test firmware, target 0.6-0.8 s).
    
    ## Battery energy
    
    Per check (mAh) x checks per day at the server's interval, against the sleep floor (test031); the ENERGY.txt line
    for the always-on live view.

## Runs

### Automated run 2026-11-12 (run.py, 71 s): pass

10/10 checks pass.


### Automated run 2026-11-12 (run.py, 66 s): **FAIL**

9/10 checks pass. Failed: the BL616's free heap kept ((169552, 140712) -> (156048, 126460)).


### Automated run 2026-11-12 (run.py, 66 s): pass

10/10 checks pass.


### Automated run 2026-11-12 (run.py, 70 s): pass

10/10 checks pass.


### Automated run 2026-11-11 (run.py, 68 s): pass

10/10 checks pass.


### Automated run 2026-11-11 (run.py, 79 s): **FAIL**

9/10 checks pass. Failed: PUT checkin 5: check-ins 5 s + the check-in apart (gaps [9, 12] s).


### Automated run 2026-11-10 (run.py, 72 s): pass

10/10 checks pass.


### Automated run 2026-11-10 (run.py, 81 s): pass

10/10 checks pass.


### Automated run 2026-11-10 (run.py, 69 s): **FAIL**

8/10 checks pass. Failed: PUT checkin 5: check-ins 5 s + the check-in apart (gaps [12, 6] s); the BL616's free heap kept ((170300, 141456) -> (159752, 141420)).


### Automated run 2026-11-10 (run.py, 61 s): pass

10/10 checks pass.


### Automated run 2026-11-10 (run.py, 68 s): **FAIL**

9/10 checks pass. Failed: PUT checkin 5: check-ins ~5 s apart (gaps [6] s).


### Automated run 2026-11-10 (run.py, 65 s): pass

10/10 checks pass.


### Automated run 2026-11-10 (run.py, 63 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 68 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 65 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 75 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 209 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 218 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 208 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 212 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 1 s): **FAIL**

not run: the board runs setup, not the full firmware


### Automated run 2026-11-09 (run.py, 212 s): **FAIL**

9/10 checks pass. Failed: the BL616's free heap kept ((191460, 191460) -> (179988, 160964)).


### Automated run 2026-11-09 (run.py, 215 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 335 s): **FAIL**

6/10 checks pass. Failed: PUT checkin 20: the board on it within one old interval (60 s) (not in 70 s); PUT checkin 20: check-ins ~20 s apart (gaps [] s); a reset with the server down: the board keeps the server's 20 s (not the 3600 s default) (check-in every 60 s after the reset); the BL616's free heap kept ((206788, 196316) -> (179988, 160960)).


### Automated run 2026-11-09 (run.py, 336 s): **FAIL**

6/10 checks pass. Failed: PUT checkin 20: the board on it within one old interval (60 s) (not in 70 s); PUT checkin 20: check-ins ~20 s apart (gaps [] s); a reset with the server down: the board keeps the server's 20 s (not the 3600 s default) (check-in every 60 s after the reset); the BL616's free heap kept ((206788, 196316) -> (179988, 160964)).


### Automated run 2026-11-09 (run.py, 215 s): pass

10/10 checks pass.


### Automated run 2026-11-09 (run.py, 338 s): **FAIL**

6/10 checks pass. Failed: PUT checkin 20: the board on it within one old interval (60 s) (not in 70 s); PUT checkin 20: check-ins ~20 s apart (gaps [] s); a reset with the server down: the board keeps the server's 20 s (not the 3600 s default) (check-in every 60 s after the reset); the BL616's free heap kept ((191460, 191460) -> (180000, 160976)).


### Automated run 2026-11-08 (run.py, 216 s): pass

10/10 checks pass.


### Automated run 2026-11-08 (run.py, 3 s): **FAIL**

not run: the board runs setup, not the full firmware


### Automated run 2026-11-08 (run.py, 215 s): pass

10/10 checks pass.


### Automated run 2026-11-07 (run.py, 226 s): pass

10/10 checks pass.


### Automated run 2026-11-07 (run.py, 223 s): pass

10/10 checks pass.


### Automated run 2026-11-07 (run.py, 278 s): pass

10/10 checks pass.


### Automated run 2026-11-07 (run.py, 279 s): **FAIL**

8/10 checks pass. Failed: a reset with the server down: the board keeps the server's 20 s (not the 3600 s default) (check-in every 0 s after the reset); its CHECK reached the running K230 (job: CHECK rtc), the K230 still on (no CHECK line).


### Automated run 2026-11-07 (run.py, 272 s): pass

9/9 checks pass.


No run yet.

## Obstacles

- **2026-11-11 05:31 UTC, gaps 9 and 12 s for check-ins every 5 s (test side).** A gap is the 5 s interval plus the next
  check-in (Wi-Fi join, DHCP, TLS); one slow join made it 12 s against the bound of 9. The bound is now 15 s, still far
  from the 60 s default the step tells apart.

No obstacle recorded yet.

## 2026-11-10 08:49 UTC+2: FAIL 7/8 (essential suite), fixed in the test

"check-ins ~5 s apart: gaps [6]": only two check-ins seen in the 20 s window. The server's history shows the board on
its interval: 06:48:05, :11, :18, :26 UTC - the interval counts from the end of a check-in, so 5 s + the check-in's
1-3 s = 6-8 s apart, and the third fell just after the window. Window 30 s, gaps 4-9 s accepted.
