# test010: K230 <-> BL616 UART link, 2026-10-18

Status: **pass** (2026-10-18).

K230 UART1 (IO40 TX, IO41 RX, `/dev/uart1`) to BL616 UART0 (IO22 RX, IO21 TX), 115200 8N1.

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

K230 UART1 driver; BL616 PT_TEST_UART.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Direction | How | Result | |
|---|---|---|---|
| K230 -> BL616 | BL616 `pt_uart on`, K230 reset: the agent sends `READY` | `$1,READY*FC` .. `$13,READY*0F`, checksums intact; each frame 5x (resent while no ACK comes) | pass |
| BL616 -> K230 | BL616 sends a line per second; K230 `cat /dev/uart1` | `LEAKCAM BL616 uart test 27` .. `40`, consecutive, none lost | pass |

**Pass.**

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | **MISSING** (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | **MISSING** (2026-10-21); fill on a re-run | - | |
| Test run time | **MISSING** (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | **MISSING** (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | **MISSING** (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | **MISSING** (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | **MISSING** (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

The agent's READY on the link marks the end of the boot (test022's last milestone); link init itself negligible.

## Battery energy

n/a: the link's own current is negligible next to the K230 running; its time cost is test018.

## Obstacles

- **Each READY 5 times** -> by design: the agent resends while no ACK comes (the test firmware sends none).
- **`cat /dev/uart1` hangs the shell (no Ctrl-C)** -> reset the K230 from the BL616 (`pt_hold`, `pt_run`).
- **`killall leakcam_agent` did not stop it** -> still listed afterwards; it did not get in the way.
