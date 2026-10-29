# test024: K230 heartbeat to the BL616 (K230_ALIVE)

Status: **pass** (2026-10-25): 8 boots, half-period 500 / 500 / 500 ms (min / avg / max), no back-feed after
`pt_off`.

The agent toggles K230 GPIO2 every 500 ms (net K230_ALIVE -> BL616 IO01, R32 100 k to 3V3); the BL616 ends a
session when the edges stop for 2 s. Without it a product session never ends (or ends early).

## Setup

BL616 test firmware with `pt_alive` (busy-polls IO01, timestamps every edge), K230 NAND test image with the agent.
Automated: run.py, 8 boots with `pt_alive 10` after each, then `pt_off` and `pt_status`. Probes: Metrics.

## Needs (build features and dependencies)

K230 GPIO (heartbeat on GPIO2), the agent; BL616 PT_TEST_POWER + pt_alive.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. BL616 test firmware: `pt_on`, `pt_run`; K230 boots the image with the agent.
2. BL616 `pt_mon on`: K230_ALIVE must change every ~0.5 s while the agent runs.
3. Stop the agent (or let the hook end): edges stop, the level stays.
4. `pt_off`: K230_ALIVE low, no back-feed (3V3 sense at 0 of 5).

## Pass

Edges at 1 Hz +-10 % while the agent runs, none after it stops; low with the rails off.

## Result

- Half-period 500 / 500 / 500 ms over 8 boots; after `pt_off` K230_ALIVE low, 0 of 5 pull-ups high (no back-feed)
  (2026-10-25, run.py).
- K230_ALIVE is high (R32 pull-up) until the agent drives GPIO2; first edge 984 ms after the reset release (the
  agent's start); the first half-period is 1 ms (`alive_open` sets 0, the thread's first toggle sets 1 at once)
  (2026-10-21).
- The edges stop when the agent ends its session (~25 s after boot with the test firmware, which does not answer the
  link: READY 15 x 5 tries x 300 ms = 22.5 s, then hook, WIFI and SLEEP unanswered); none afterwards (2026-10-21).
- No leak on either chip over 8 boots (2026-10-25).

Open:
- 3V3 discharge 10 ms after `pt_off` once the K230 had run (twice) vs 56-82 ms through RD1/RD2 with the K230 in reset
  (test003): a running/stopped K230 loads 3V3 harder, likely harmless; repeat from reset to compare (2026-10-21).

## Metrics (2026-10-21, BL616 test firmware 16:56:59 build, K230 NAND test image)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured: one boot; first heartbeat edge 0.98 s after reset release | test022: 0.88 s to app init | the agent starts ~0.1 s after app init |
| This part's boot share | ~0 (GPIO open + one write) | - | |
| Test run time | 35 s (20:04:18-20:04:53) + checks to 20:05:32 | - | |
| Setup time | 19:52-20:04, 12 min: pt_alive written + built (13 s build) 19:52-19:57; flash 1 failed 19:57:14-19:58:14 (permissions, HISTORY.md); ROM re-attached 20:00:20; flash 2 (4 s) 20:02:22-20:02:26, waited for EN + console to 20:04:09 | - | the flash itself is 4 s; the rest is waiting for hands and the script bug |
| heartbeat period and jitter | half-period 500 / 500 / 500 ms (min / avg / max over 8 boots) (2026-10-25) | 500 ms, timeout 2 s | 100 % of the set period, 4x margin to the timeout |
| Current idle / during | not measured: no meter reading asked (my omission); msh idle 202 mA (test023) | floor (test031) | |
| Voltage under load (min) | VBAT 4.34 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: one GPIO write per 500 ms, below the meter's resolution | - | |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | die 42.2 / 42.8 / 42.8 C; board 32.3 / 33.0 / 33.0 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 207136 / min 204396 / 207136 B, blocks 19 -> 19, deleted 0; K230 heap available 7025152 B steady; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB; BL616 test firmware 121 KB (+ boot2 53 KB) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO); BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |
| Features over the minimal image | n/a: the heartbeat is part of the product agent (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (a 1 Hz GPIO toggle): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

First heartbeat edge 0.98 s after the reset release (the agent's start); the heartbeat's own share ~0 (GPIO open + one
write) (2026-10-21).

## Battery energy

n/a: one GPIO toggling every 500 ms.
