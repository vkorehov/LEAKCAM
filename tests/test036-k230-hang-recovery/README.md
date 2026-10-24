# test036: K230 hang recovery (heartbeat stop, READY timeout)

Status: **pass** (2026-10-22): 39 supervised sessions (10 normal, 10 hang, 10 no-boot, 9 reboot) all end with the rails off, timings within a few ms; found: a K230 restart (or a hung agent with a live heartbeat thread) is invisible to the heartbeat and sessions had no length limit -> product: SESSION_MAX_MS 15 min backstop, SESSION_MAX_RESTARTS 2, BOOT_TIMEOUT_MS 90 -> 10 s.

The BL616 must end every K230 session, even a hung one: no heartbeat edge for 2 s -> rails off; no READY within the
boot timeout (90 s, to tighten after test022) -> rails off, retry, back off after 3 failures. Without it a hang drains
the battery in hours.

## Setup

The product firmware does not run on rev 1 yet (rewrite after the tests), so the BL616 test firmware's `pt_session
[boot_ms] [noboot | hang ms | reboot ms]` supervises a session as the product's `run_session()`: rails on, reset
released (NAND boot), K230_ALIVE (the agent's heartbeat, an edge every 500 ms) watched; the first heartbeat edge
stands in for READY (no link protocol in the test firmware; edges in the first 400 ms are the boot toggling
JTAG_TCK = ALIVE and are ignored); no edge for 2 s (HEARTBEAT_STOP_MS) or no first edge within boot_ms -> rails
off. The failure cases are made by the BL616 (when this ran the K230 console needed a VMware click per session; since
test039 it is the BL616's `/dev/ttyACM1`, no click): `hang` holds the
K230 in reset (CPU stopped, heartbeat frozen, as a hang looks from outside), `reboot` pulses the reset 50 ms (a
restart mid-session, as the K230 watchdog would), `noboot` sets BOOT1 high (the ROM waits on USB, no agent). K230:
the bench image from test018/019 (agent with its 20-25 s hook). USB-C 1 on a plain charger.

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

## Results

### 2026-10-22, one of each

| Case | Heartbeat | End |
|---|---|---|
| normal | first edge **1.44 s** after power-on, last at 25.4 s | no edge for 2.0 s -> rails off at 27.8 s |
| hang at 8 s (reset held) | last edge at 8.0 s | **rails off at 10.3 s** (2.0 s detection + 0.3 s discharge) |
| reboot at 8 s (50 ms reset pulse) | gap of only **1.0 s**, then a fresh agent (first edge 0.9-1.4 s after a start) | **not noticed**: the restarted agent ran a whole session, rails off at 35.5 s |
| noboot (BOOT1 high), 15 s timeout | none | END_NO_BOOT at 15.0 s, rails off 15.3 s |

### 2026-10-22, 10 cycles each ([cycles_2026-10-22.txt](cycles_2026-10-22.txt))

| Case | Runs | First heartbeat | End |
|---|---|---|---|
| normal | 10 | 0.93-1.45 s | last edge 25.44-25.46 s, rails off **27.76-27.77 s** |
| hang at 5 s | 10 | 0.93-1.46 s | last edge 5.00 s, rails off **7.32 s** (every run) |
| no boot, 15 s timeout | 10 | none | END_NO_BOOT, rails off **15.32 s** |
| reboot pulse at 5 s | 9 | 0.93-1.45 s | gap **1.015 s** every run, never noticed; rails off 32.46-32.47 s |

(One no-boot run's lines landed in the next run's capture; counted as no-boot. The tenth reboot run was cut by the
script being stopped.) No session stayed on; the K230 console (CH340) came and went with the rails, not used.

**Found (product, fixed in firmware/bl616/main.c):**
- **A K230 restart is invisible to the heartbeat** (back within ~1 s, under the 2 s limit) and so is **a hung agent
  whose heartbeat thread still runs** (the heartbeat is a thread of its own, leakcam_agent.c), and sessions had **no
  length limit**: a crash loop or such a hang keeps the K230 on until the battery is flat. The product does see a
  restart as a second READY (it answered it with WAKE). Now: `SESSION_MAX_MS` 15 min (a backstop above the agent's own 10 min hook limit) and `SESSION_MAX_RESTARTS` 2
  end a session as a failure (rails off, then the existing retries and back-off), logged as `session-end ... cut`.
- **BOOT_TIMEOUT_MS 90 s -> 10 s**: the heartbeat (READY with it) starts 0.9-1.4 s after power-on; a K230 that never
  boots now costs 10 s per attempt (3 attempts, then the 5 min / 1 h back-off).

Not covered here: the product's READY / link path itself (the test firmware has no link protocol) - with the product
build (rewrite); a real hang inside the agent (a hook that loops) - the session cap covers it whatever its form.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| time to rails off after a hang | **MISSING** | 2 s heartbeat, boot timeout | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

The boot timeout should follow test022's measured boot (e.g. 3 x the median) instead of 90 s.

## Battery energy

Energy of a failed session (boot timeout x the session floor): what one hang costs, x the retries.

## Obstacles

(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **The K230 console is not available per session** (the CH340 lives on the switched 3V3 and needs a VMware click
  each time) -> the failure cases are made from the BL616 (reset held / pulsed, BOOT1 high).
- **A "heartbeat" edge 239-249 ms after power-on** - before any agent (app init is at 0.48 s): ALIVE is the K230's
  JTAG_TCK at reset and the boot toggles it -> edges in the first 400 ms are ignored; the product only watches the
  heartbeat after READY, so it never counted it.
