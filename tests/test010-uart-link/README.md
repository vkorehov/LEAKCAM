# test010: K230 <-> BL616 UART link

Status: **pass** (2026-10-25): 8 boots, 112 READY frames K230 -> BL616 with valid checksums, BL616 -> K230 echo clean.

K230 UART1 (IO40 TX, IO41 RX, `/dev/uart1`) to BL616 UART0 (IO22 RX, IO21 TX), 8N1;
first run at 115200 (2026-10-18), the link runs 2 Mbaud (test018, test039).

## Setup

K230 with the leakcam_agent image, BL616 bl616_powertest (PT_TEST_UART), the link above; BL616 console for `pt_uart`.

## Needs (build features and dependencies)

K230 UART1 driver; BL616 PT_TEST_UART.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. K230 -> BL616: BL616 `pt_uart on`, reset the K230: the agent sends `READY` frames.
2. BL616 -> K230: the BL616 sends a numbered line per second; K230 `cat /dev/uart1`.

## Pass

Step 1: every READY frame arrives with an intact checksum; step 2: the lines arrive consecutive, none lost.

## Result

- K230 -> BL616: 112 READY frames over 8 boots, every checksum valid (run.py, 2026-10-25); each frame is resent while
  no ACK comes (5x with the test firmware, which sends none) (first run, 2026-10-18).
- BL616 -> K230: consecutive lines, none lost (first run, 2026-10-18); echo clean (run.py, 2026-10-25).

Open:
- Current floor / typical / peak not read (Metrics **MISSING**).

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.46 / 0.46 s (median / max of 8; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.45, msh 0.46); longest step to mounted 0.20 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: the agent sends READY right after app init; the link adds no boot step (2026-10-25) | - | |
| Test run time | 243 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | link at 2 Mbaud (test039); 112 READY frames, every checksum valid (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | K230 at msh idle (agent running) 0.220 A at 5 V (USB-C 1 VBUS meter, test006, 2026-10-25); the link frames add less than the meter's 1 mA step | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 7 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: below the meter's step next to the K230 (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: a few frames per session (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 29.4 / 34.9 / 34.6 C; board 21.6 / 26.7 / 26.7 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7313600 B steady; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | link_mux in the kernel: 2.4 KB code + 3.5 KB RAM, registers at boot with the other drivers (no visible ms); part of the product (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (the UART line rate (2 Mbaud)): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

The agent's READY on the link marks the end of the boot (test022's last milestone); link init itself negligible.

## Battery energy

n/a: the link's own current is negligible next to the K230 running; its time cost is test018.
