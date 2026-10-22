# test036: K230 hang recovery (heartbeat stop, READY timeout)

Status: **planned** (2026-10-21).

The BL616 must end every K230 session, even a hung one: no heartbeat edge for 2 s -> rails off; no READY within the
boot timeout (90 s, to tighten after test022) -> rails off, retry, back off after 3 failures. Without it a hang drains
the battery in hours.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

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

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| time to rails off after a hang | not measured yet | 2 s heartbeat, boot timeout | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

The boot timeout should follow test022's measured boot (e.g. 3 x the median) instead of 90 s.

## Battery energy

Energy of a failed session (boot timeout x the session floor): what one hang costs, x the retries.

## Obstacles

None yet.
