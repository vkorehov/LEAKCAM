# test023: K230 heartbeat to the BL616 (K230_ALIVE)

Status: **pass** (2026-10-21).

The agent toggles K230 GPIO2 every 500 ms (net K230_ALIVE -> BL616 IO01, R32 100 k to 3V3); the BL616 ends a
session when the edges stop for 2 s. Without it a product session never ends (or ends early).

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

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

## Results (2026-10-21, BL616 test firmware with `pt_alive`, K230 NAND test image)

`pt_on`, `pt_run`, at once `pt_alive 30` (BL616 busy-polls IO01, timestamps every edge):

| Reading | Value | |
|---|---|---|
| Level before the agent runs | high (R32 pull-up, GPIO2 not yet driven) | |
| First edge after the reset release | 984 ms (agent drives GPIO2 low at start) | = the agent's start |
| Half-period min / avg / max | 1 / 490 / 502 ms (1.02 Hz) | pass (+-10 %); the 1 ms is the start: `alive_open` sets 0, the thread's first toggle sets 1 at once |
| Edges in 30 s | 51: edges stop ~25 s after boot | pass: the agent ended its session (below) |
| `pt_alive 5` afterwards | 0 edges, low | pass |
| `pt_off`, then `pt_status` | 0 of 5 pull-ups high, K230_ALIVE low | pass: no back-feed |

Why it stops after ~25 s: the test firmware does not answer the link, so READY fails 15 x 5 tries x 300 ms = 22.5 s,
then the hook, WIFI and SLEEP (each unanswered, 1.5 s) -> the agent stops the heartbeat; the product BL616 would cut
the power 2 s later. As designed.

**Pass.**

## Metrics (2026-10-21, BL616 test firmware 16:56:59 build, K230 NAND test image)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured: one boot; first heartbeat edge 0.98 s after reset release | test021: 0.88 s to app init | the agent starts ~0.1 s after app init |
| This part's boot share | ~0 (GPIO open + one write) | - | |
| Test run time | 35 s (20:04:18-20:04:53) + checks to 20:05:32 | - | |
| Setup time | 19:52-20:04, 12 min: pt_alive written + built (13 s build) 19:52-19:57; flash 1 failed 19:57:14-19:58:14 (permissions, above); ROM re-attached 20:00:20; flash 2 (4 s) 20:02:22-20:02:26, waited for EN + console to 20:04:09 | - | the flash itself is 4 s; the rest is waiting for hands and the script bug |
| heartbeat period and jitter | half-period 490 ms avg, 502 max | 500 ms, timeout 2 s | 98 % of the set period, 4x margin to the timeout |
| Current idle / during | not measured: no meter reading asked (my omission); msh idle 202 mA (test022) | floor (test030) | |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: one GPIO write per 500 ms, below the meter's resolution | - | |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB; BL616 test firmware 121 KB (+ boot2 53 KB) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, --gc-sections (no -ffunction-sections, no LTO); BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |

## Boot time

Time from RT-Smart to the first heartbeat edge (the agent's start).

## Battery energy

n/a: one GPIO toggling every 500 ms.

## Obstacles

- **First `make flash` flashed nothing** (19:57:14-19:58:14) -> the ROM was attached at 19:57:21 (dmesg), but
  /dev/ttyACM0 was root:dialout and not writable; `bl616_isp.py` had spent its one-shot hint on the "waiting" line
  and timed out silently after 60 s -> fixed: the script grants itself access (`sudo -n setfacl`) on every
  re-enumeration, separate hints (tested with `--wait-app`).
- **That failure looked like success** (my mistake) -> the background command was piped through `tail`, exit 0 ->
  never pipe flash/build commands; read the log.
- **The flash looked "so long"** (my mistake) -> after the 4 s flash, `make flash` waits for the EN tap and the
  console, and I had not told the user first -> ask for every hand action before the waiting command (skill
  board-bringup).
- **3V3 discharge 10 ms after `pt_off`** (twice, K230 had run; test003: 56-82 ms through RD1/RD2 with the K230 in
  reset) -> open: a running/stopped K230 loads 3V3 harder, likely harmless; repeat from reset to compare.
- **A Docker `make -j5` container up 11 h** found during the flash (`docker ps`) -> open: left from an earlier build,
  may slow the qemu builds; asked the user before stopping it.
