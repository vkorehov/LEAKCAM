# test024: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 226 s): pass

8 boots, pt_alive 10 after each: half-period 500 / 500 / 500 ms (min / avg / max over 8 boots, agent 500); after pt_off no back-feed (0 of 5 pull-ups).


### Automated run 2026-10-25 (run.py, 227 s): pass

8 boots, pt_alive 10 after each: half-period 500 / 500 / 500 ms (min / avg / max over 8 boots, agent 500); after pt_off no back-feed (0 of 5 pull-ups).

### Results (2026-10-21, BL616 test firmware with `pt_alive`, K230 NAND test image)

Status: **pass** (2026-10-21).

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

Superseded metric values:

- heartbeat period and jitter (2026-10-21): half-period 490 ms avg, 502 max; note "98 % of the set period, 4x margin
  to the timeout"

#### Old section placeholders

- Setup: **MISSING**: hookup, firmware / image, and the probes named in Metrics.
- Boot time: Time from RT-Smart to the first heartbeat edge (the agent's start).

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
