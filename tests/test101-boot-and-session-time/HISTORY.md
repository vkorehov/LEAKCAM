# test101: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.8, 5.3.12)

The first plan and the 2026-10-30 result measured a session of the first product's K230 agent (`leakcam_agent`,
attic/firmware) under the BL616 test firmware's `pt_session`: READY 0.87 s, then **24.5 s of agent work** (leakcam_wake's
camera and Wi-Fi tries and their timeouts on a bench with no camera and no server), the last heartbeat 25.36 s, rails
off 27.68 s. The rewrite has no agent: one product program `/app/leakcam` on every power-up, and a session on battery is
READY -> WAKE -> the job -> `DONE,<cam0>,<cam1>` -> rails off. The boot part (power-on -> READY) still stands for the
boot chain; the session numbers after READY and the energy built on them (1.71 mAh per wake, 4 report sessions a day
6.9 mAh/day) are superseded and re-measured on the product program. The old README, whole:

    # test101: session length (wake to power-off)

    Status: **pass, steps 1-2** (2026-10-30, run.py): 10 of 10 sessions, power-on -> READY 0.87 s, rails off 27.68 s;
    step 3 needs the link protocol in the BL616 test firmware.

    The whole session: wake, K230 boot (measured on its own in test022), READY, the agent's work, the heartbeat
    stopping, power-off. With test102 it decides the battery cost of a wake.

    ## Setup

    BL616 test firmware (`bl616_powertest`: `pt_session`, `pt_uart on` for the link bridge) on PR1; the K230 test image
    on NAND (test019); both consoles (`/dev/ttyACM0`, `/dev/ttyACM1`). USB-C 1 in (USB power). `run.py` runs it all.

    ## Needs (build features and dependencies)

    BL616: K230 power path, the heartbeat watch (`pt_session`), the UART0 link bridge; K230: the boot chain (test022),
    `link_mux`, `leakcam_agent` (READY, heartbeat) and what it starts in a session.

    (What this test uses; the union over all tests is the feature set the minimal image keeps.)

    ## Steps

    1. BL616 test firmware, console of both recorded with timestamps (`console_record.py`): `pt_on`, `pt_run` at t0;
       K230 milestones: SPL, OpenSBI, RT-Smart banner, agent READY on the link.
    2. Repeat 10 times (cold, rails off >= 5 s between).
    3. With the link protocol in the BL616 test firmware (from firmware/bl616_common): its own "READY <ms> after power-on" and the session end line.

    ## Pass

    Recorded: rails-on -> READY (median, max), and what dominates it (DDR training, NAND read, mounts); first
    candidates to shorten.

    ## Result

    run.py, 2026-10-30, BL616 test firmware `pt_session`, K230 test image; 10 cold sessions, rails off 5 s between:

    | From power-on (median of 10, spread <= 13 ms) | s |
    |---|---|
    | U-Boot SPL banner | 0.22 |
    | OpenSBI | 0.33 |
    | RT-Smart banner | 0.63 |
    | file systems mounted | 0.84 |
    | msh | 0.85 |
    | agent READY frame on the link | 0.87 |
    | first heartbeat edge (BL616-timed) | 1.36 |
    | the agent's last heartbeat edge | 25.36 |
    | rails off (2 s after the last edge) | 27.68 |

    - The BL616's heap stays level over the 10 sessions (131124 B free each time): no leak.

    Open:
    - The agent works **24.5 s** after READY with no camera and no server on the bench: what fills it (leakcam_wake's
      camera and Wi-Fi tries and their timeouts) is not split yet; it is most of a session's energy (test102).
    - Step 3: the BL616's own "READY <ms>" and the session end line need the link protocol in the test firmware.

    ## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

    Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

    | Metric | Value | Top / budget | Note |
    |---|---|---|---|
    | Boot, power-on -> READY (median of 5) | power-on -> READY frame 0.87 / 0.87 s (median / max of 10; SPL 0.22, OpenSBI 0.33, RT-Smart 0.63, mounted 0.84, msh 0.85; first heartbeat 1.36 s) (2026-10-30) | target (test022) | longest step: |
    | This part's boot share | the session after READY: 24.5 s of agent work, then 2 s to rails off (2026-10-30) | - | |
    | Test run time | 419 s automated (run.py, 2026-10-30), no setup | - | setup (flashing, plugging, waiting) separately |
    | session length per step | wake -> rails off 27.68 / 27.68 s (median / max of 10); the agent's last heartbeat 25.36 s (2026-10-30) | - | % of the top |
    | Current idle / during (min / avg / peak) | session, agent working 228 mA; boot ~87 mA (held in reset, test021); 220 mA at msh idle (test006) - test102's readings of these sessions (USB-C 1 meter, 2026-10-30) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
    | Voltage under load (min) | n/a: on USB power (the charger rail); no battery measurements (user, 2026-10-30) | cutoff 3.10 V (bug-004) | at the probes above |
    | Power, energy per operation | one wake 1.71 mAh at the board (test102: 0.87 s boot, 24.5 s agent at 228 mA, 2.3 s at 220 mA) | - | scripts/energy.py |
    | Per day | 4 report sessions a day: 6.9 mAh/day (test102) | ENERGY.txt line | n per day |
    | Temperature die / board (start / peak / end) | die n/a: K230 off; board 36.3 / 39.2 / 38.9 C (AHT20) (2026-10-30) | K230 limit (datasheet) | chip_temp, AHT20 |
    | Memory BL616 / K230 (start / min / end), leak | BL616 free 131124 / min 105196 / 131124 B, blocks 106 -> 106, deleted 0; BL616 no leak (mem_leak.py, 10 snapshots) (2026-10-30) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
    | Image (sizes) | BL616 test firmware 1309616 B (2026-10-30, on the board at the run) | minimal image (rule) | the image this run used |
    | Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-10-30) | smallest code (rule) | |
    | Features over the minimal image | BL616 test firmware `pt_session` + the UART0 link bridge; K230 test image (test019) as test022 | test008 floor | bytes, boot ms, RAM, current |
    | Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

    ## Boot time

    Boot is test022; this test adds the session after READY.

    ## Battery energy

    Energy per whole session (with test102): the number the budget multiplies by the wakes per day.

## Runs

### Automated run 2026-11-12 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-11 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-10 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-09 (run.py, 43 s): pass

4/4 checks pass.


### Automated run 2026-11-09 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-09 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-09 (run.py, 42 s): pass

4/4 checks pass.


### Automated run 2026-11-09 (run.py, 0 s): **FAIL**

not run: the board runs setup, not the full firmware


### Automated run 2026-11-08 (run.py, 40 s): pass

4/4 checks pass.


### Automated run 2026-11-08 (run.py, 107 s): **FAIL**

3/6 checks pass. Failed: session 1: READY logged (None); session 2: READY logged (None); session 3: READY logged (None).


### Automated run 2026-11-08 (run.py, 2 s): **FAIL**

not run: no pt_boot, pt_uart on the BL616.


### Automated run 2026-11-07 (run.py, 1006 s): **FAIL**

0 of 10 sessions complete; session 1: incomplete (first last off); session 2: incomplete (first last off); session 3: incomplete (first last off); session 4: incomplete (first last off); session 5: incomplete (first last off); session 6: incomplete (first last off); session 7: incomplete (first last off); session 8: incomplete (first last off); session 9: incomplete (first last off); session 10: incomplete (first last off)


### Automated run 2026-10-30 (run.py, 422 s): pass

10 of 10 sessions complete: rails off 27.93-27.94 s after power-on.


### Automated run 2026-10-30 (run.py, 419 s): pass

10 of 10 sessions complete: rails off 27.67-27.68 s after power-on.


No runs yet.

## Obstacles

- the BL616 firmware has no pt_boot, pt_uart: flash the test firmware that has it (2026-11-08, automated run)

None recorded.
