# test036: K230 hang recovery (heartbeat stop, READY timeout)

Status: **pass** (2026-10-25): 12 supervised sessions (normal, hang, reboot, no-boot) all end with the rails off; a
hang at 5 s -> rails off 1.84-1.85 s later; a K230 restart is invisible to the heartbeat, covered in the product by
SESSION_MAX_MS 15 min and SESSION_MAX_RESTARTS 2 (2026-10-22).

The BL616 must end every K230 session, even a hung one: no heartbeat edge for 2 s -> rails off; no READY within the
boot timeout (BOOT_TIMEOUT_MS 10 s) -> rails off, retry, back off after 3 failures. Without it a hang drains the
battery in hours.

## Setup

The product firmware does not run on rev 1 yet (rewrite after the tests), so the BL616 test firmware's `pt_session
[boot_ms] [noboot | hang ms | reboot ms]` supervises a session as the product's `run_session()`: rails on, reset
released (NAND boot), K230_ALIVE (the agent's heartbeat, an edge every 500 ms) watched; the first heartbeat edge
stands in for READY (no link protocol in the test firmware; edges in the first 400 ms are the boot toggling
JTAG_TCK = ALIVE and are ignored); no edge for 2 s (HEARTBEAT_STOP_MS) or no first edge within boot_ms -> rails
off. The failure cases are made by the BL616: `hang` holds the K230 in reset (CPU stopped, heartbeat frozen, as a
hang looks from outside), `reboot` pulses the reset 50 ms (a restart mid-session, as the K230 watchdog would),
`noboot` sets BOOT1 high (the ROM waits on USB, no agent). K230: the bench image from test018/019 (agent with its
20-25 s hook). USB-C 1 on a plain charger. Probes: Metrics.

## Needs (build features and dependencies)

BL616 product power manager (heartbeat watch, READY timeout), K230 GPIO heartbeat, the agent.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Product firmware: stop the K230 agent (or a test hook that hangs): power-off after 2 s of no heartbeat, logged.
2. An image that never sends READY: power-off at the boot timeout, retries, the back-off interval.
3. The K230 watchdog's own reset (20 s, test015) during a session: what the BL616 sees, and the session's end.
4. 10 cycles each; the battery path returns to HBN every time (test031 current after).

## Pass

Every hang ends in rails off within its timeout, no session runs on; retries and back-off as designed.

## Result

- Every case ends with the rails off (2026-10-25, run.py, 12 sessions; 2026-10-22, 39 sessions): normal: first
  heartbeat 1.53 s, rails off 27.86 s (2 s after the agent's last edge); hang at 5 s: rails off 6.84-6.85 s; no boot
  (15 s test timeout): rails off 15.31 s; reboot pulse at 5 s: rails off 32.47 s.
- A K230 restart (back within ~1 s, gap 1.015 s, under the 2 s limit) and a hung agent whose heartbeat thread still
  runs are invisible to the heartbeat; the product sees a restart as a second READY (2026-10-22). Fixed in
  firmware/bl616/main.c: `SESSION_MAX_MS` 15 min (above the agent's own 10 min hook limit) and
  `SESSION_MAX_RESTARTS` 2 end a session as a failure (rails off, then retries and back-off), logged as
  `session-end ... cut`.
- BOOT_TIMEOUT_MS 90 s -> 10 s (the heartbeat starts 0.9-1.5 s after power-on): a K230 that never boots costs 10 s per
  attempt, 3 attempts, then the 5 min / 1 h back-off (2026-10-22, firmware/bl616/main.c).
- K230_ALIVE toggles 239-249 ms after power-on before any agent (the K230's JTAG_TCK at reset); the product watches the
  heartbeat only after READY (2026-10-22).
- No BL616 leak over 12 sessions (2026-10-25).

Open:
- Not covered: the product's READY / link path itself (the test firmware has no link protocol) - with the product
  build (rewrite); a real hang inside the agent (a hook that loops) - the session cap covers it whatever its form.
- Steps 2-4 (retries / back-off interval, the K230 watchdog's own reset, HBN current after) with the product firmware.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | power-on -> first heartbeat (READY stand-in) 1.53-1.53 s (3 sessions) (2026-10-25) | target (test022) | longest step: |
| This part's boot share | n/a: supervision runs on the BL616 beside the boot (2026-10-25) | - | |
| Test run time | 345 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| time to rails off after a hang | 1.84-1.85 s after the hang at 5 s (2026-10-25) | 2 s heartbeat, boot timeout | % of the top |
| Current idle / during (min / avg / peak) | a session is the K230 at msh idle: 0.220 A; after the rails go off 0.061 A (both test006, 2026-10-25) (2026-10-25, from recorded readings) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: pt_session runs without the K230 console (no ADC read); VBAT at the same load (msh idle) 4.34 V (test007) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | a hang costs the msh-idle current (220 mA, test006) for 2.3 s of detection + discharge: 141 uAh per hang (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: hangs are faults, not daily events (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 29.6 / 33.3 / 32.8 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; BL616 no leak (mem_leak.py, 12 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1114864 B (2026-10-25, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

The product boot timeout is BOOT_TIMEOUT_MS 10 s; power-on -> first heartbeat (READY stand-in) 1.53 s (2026-10-25).

## Battery energy

A hang costs the msh-idle current (220 mA, test006) for 2.3 s of detection + discharge: 141 uAh per hang (2026-10-25,
Metrics).
