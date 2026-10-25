# test105: maximum current - can the battery carry everything at once?

Status: **handover runs 1-2 done** (2026-10-25): USB -> cell under every camera-free load (1.06 A at 5 V), no power fault, K230 alive at 60.2 C afterwards; "no reset on the cell" still to prove (next run: unplug only USB-C 1, keep PR1). Planned since 2026-10-25 ( user, after test009: the cell carried K230 + both LED chains, ~1.15 A, on a USB
unplug with no brownout and no visible dimming).

The worst case the cell and the power path must survive: every consumer on the board busy at the same time, on a
full and on a low cell. Rev 1 has no hardware discharge cutoff (bug-004): a sag below the rails' minimum would reset
the K230 or the BL616 mid-write. This test finds the peak current, the sag it causes, the heat it makes, and the
lowest rest voltage at which the full load still runs without a brownout.

## Setup

- Meter in the cell's + lead, **10 A range, locked** (the 600 mA range's shunt dropped the supply at 0.5 A and hid
  the LED knee, test009), jumper across it while the range or jack changes; every USB power cable out (PR1 only, the
  consoles: `/dev/ttyACM0` BL616, `/dev/ttyACM1` K230).
- VBAT at C15.1 on a second meter (minimum hold) or a scope for the dips (the LED soft start, the Wi-Fi TX bursts).
- Full cell (charged, D1 off, ~4.17 V at rest) for runs 1-3; a low cell (~3.5 V and ~3.3 V at rest, drained by
  running run 2 on battery) for run 4.
- Board in the open (no enclosure) for the first runs; then once in the closed enclosure for the heat.

## Needs (build features and dependencies)

K230 image with the test tools: `leakcam_cpubench` (test008), `memtest` (test025), `venctest` (test027), the change
net loop (test013), NAND fill (test011), `iperf` (test100), PWM shell commands (test009), `chip_temp`; BL616 test
firmware with the bridge (`pt_wifi bridge`) and `pt_session`-style supervision; the LAN PC's `pc_tcp_bench.py`
(test100). Cameras (capture + ISP) once they arrive.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

Each load is started one by one on top of the previous (the current added by each is recorded), then all together.

| # | Load | Tool | Expected |
|---|---|---|---|
| 1 | K230 idle at msh | - | 0.22 A @ 5 V (test009) |
| 2 | + CPU: both cores busy | `leakcam_cpubench` in a loop (2 instances) | |
| 3 | + DDR: memtest | `memtest` | |
| 4 | + KPU: change net back to back | test013 loop | |
| 5 | + VPU: H.265 2 x 15 fps (then the encoder's top, ~83 fps) | `venctest` | |
| 6 | + NAND: continuous writes | test011 fill | |
| 7 | + Wi-Fi: TCP upload to the PC | `iperf -c`, BL616 bridge | ~27 Mbit/s |
| 8 | + both LED chains at 100 % | `pwm_set` | +0.73 A @ 5 V (test009) |
| 9 | + cameras streaming | (after the cameras arrive) | |

Runs:
1. **Steps 1-8 on a full cell**, 30 s per step: battery current per step, VBAT minimum, resets (K230 console, BL616
   `evlog`), die / board temperature.
2. **All loads for 10 min** on a full cell: the current, VBAT and heat over time (does the K230 throttle? does the
   board reach a limit?).
3. **Cold start into full load**: rails on with the cell at rest, all loads started at once (inrush + LED soft start
   + Wi-Fi join at the same moment).
4. **Low cell**: run 1's step 8 (everything on) at 3.5 V and then 3.3 V rest: the sag under the full load against the
   3.10 V cutoff (test038) and the rails' minimum (TPS62823 >= 2.4 V, TPS63802 >= 1.3 V); the rest voltage at which
   the cut first fires under full load = the usable end of the cell.

## Pass

No brownout or reset in any run on a full cell; VBAT under the full load stays above the 3.10 V cutoff down to a
3.5 V rest voltage; no component past its limit (die temperature, LED driver, cell discharge rating ~1C continuous /
2C bursts); every figure recorded.

## Results

### Handover run 1, 2026-10-25 12:31 (`usb_handover.py 150`, 10 A meter in the USB-C 1 VBUS line, cell full)

Loads without cameras: both LED chains 100 %, Wi-Fi TCP upload to the LAN PC (iperf, ~21-25 Mbit/s), DDR + CPU
(`memtest 0`). Read on USB, then USB unplugged by the user at ~30 s and left out for over a minute.

| What | Result |
|---|---|
| USB input, every load on | **1.05-1.07 A at 5 V** (~5.3 W; ~1.25-1.3 A from a 4.1 V cell) |
| USB unplugged under that load | **no brownout**: K230 rails stayed on, the BL616 console never dropped, both LED chains the same brightness (user) for over a minute |
| Cell temperature by hand | cold (user) |
| K230 console | memtest passes 0-2 (0 errors), iperf 21-25 Mbit/s, then **silent from 19 s** (before the unplug); no SPL banner (no reset); the end-of-run `chip_temp` got no answer |
| After the run | cell recharged at 0.9 A, tapering to 0.6 A within minutes: a small top-up (~50 mAh used, ~3 %) |

- The power path passes: the cell takes ~1.3 A with every camera-free load, cold, with no reset on the USB -> cell
  switch.
- The K230 going silent is not a power fault: memtest took 109 of 128 MB (its first MMZ allocation already failed),
  and at 15 s the kernel logged "ELF loading failed" - the memory was gone. Rerun: memtest keeps 24 MB free and runs
  as a background job; the runner asks for `chip_temp` every 5 s so a stall shows within seconds.

### Handover run 2, 2026-10-25 12:53 (memtest with 24 MB reserved, `chip_temp` probe every 5 s)

| What | Result |
|---|---|
| USB input, every load on | 1.06 A at 5 V |
| Until the unplug (24 s) | 4 of 4 probes answered (39.8 -> 44.6 C), memtest passes 0-3 with 0 errors (89 MB), iperf ~28.7 Mbit/s |
| USB unplugged (USB-C 1 **and PR1**, the console cable, by the user) | no console (no /dev/ttyACM*): nothing could be read or switched off; the LEDs stayed lit, the rails on (the runner's final `pt_off` found no port) |
| USB back, ~4 min later | the K230 answered `chip_temp`: **60.2 C** after ~5 min of full load (~3 min of it on the cell); then `pt_off` by hand |

- Run 1's "silent at 19 s" was the same thing: PR1 came out with USB-C 1.
- Plugged back in while the K230 still ran: **no charging** until `pt_off` - bug-002 (the rails back-feed VUSB to
  ~2.2 V, a USB-C source waits for < 0.8 V before it turns VBUS on).
- Power: no fault in either run (rails on, LEDs lit, K230 answering afterwards).
- **Not proven yet: no K230 reset while on the cell** - a rebooted K230 also answers the probe, and lit LEDs prove
  nothing (an unconfigured PWM channel lights at full, test009). Next run: unplug **only USB-C 1**, keep PR1 (data
  only, no power): the console stays, the probes and memtest's pass count are read through the whole battery phase.

## Metrics (skill leakcam-test-metrics)

**MISSING** until the first run.

## Obstacles

- **The console went away at the unplug in both runs** (2026-10-25) -> PR1 (the console cable) was unplugged together
  with USB-C 1 -> unplug only USB-C 1 for battery runs; PR1 carries no power.
- **The K230 went silent 19 s into handover run 1** (2026-10-25) -> memtest left the system no memory (109 of 128 MB,
  a program failed to load) -> memtest with 24 MB reserved, plus a `chip_temp` liveness probe every 5 s.
