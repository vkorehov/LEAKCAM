# test105: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-30 (run.py, 767 s): **FAIL**

7 load steps on USB; then all loads 10 min: 0 K230 resets. Die 48.2-58.7 C over the steps.

- meter test105: + KPU change net in a loop: 0.242 A (read in test105-max-current)
- meter test105: + encoder 2 x 15 fps: 0.285 A (read in test105-max-current)
- meter test105: + NAND writes: 0.275 A (read in test105-max-current)
- meter test105: + Wi-Fi TCP upload: 0.395 A
- meter test105: + memtest (CPU + DDR, the memory left): 0.400 A
- meter test105: + both LED chains 100 %: 0.563 A


### Automated run 2026-10-30 (run.py, 34 s): **FAIL**

stopped at '+ KPU change net in a loop': the load did not start


### Runs 2026-10-30 afternoon: per-load currents and 10 min full load (pass on USB), after five obstacles

1. The meter cache handed the KPU step 224 mA without asking: the user's reading from the void morning run (/bin
   unmounted). Entry deleted; the cache keeps only readings of a state that really ran.
2. memtest started first took all the MMZ (69 MB): the KPU test (`mmz_allocator allocate_segment failed`) and the
   encoder (`mmz malloc 29491200 failed`) started after it failed, so the "KPU" (283 mA) and "encoder" (280 mA,
   peak 305) readings were memtest alone: discarded. memtest now last; every load's start-up output is checked
   (failed / not found / out of memory) and stops the run. The checker first stopped on the kernel's harmless
   `[W/LWP] not use fast load, out of memory` note: that line is excluded.
3. The encoder stalled at t=100 s (`out 0 fps`, enqueue failures): /sdcard was full (test028's files + the
   streams); test027 HISTORY. The KPU / encoder / NAND readings were taken before it (15/15 fps at the time).
4. The Wi-Fi step found no interface (`ifconfig: network interface device list error`): MMC0 is probed only on
   request; run.py now sends `bl616_rescan` (card bound in 43 ms, address in < 1 s).
5. The harness called the run a memory leak: its verdict ran over every sample, including the loads starting.
   `Run.leak_from` now marks the repeated phase (test105: the 10 min); over it no leak.

Side readings (not a step of the final order): KPU + encoder + NAND + memtest without Wi-Fi 305 mA, peak 320.

### Run 2026-10-30: void (NAND not mounted, no Wi-Fi bridge)

- The K230 booted with `/bin` and `/sdcard` unmounted (kernel `unknown raw ID aa220000`, test017 HISTORY), so every
  load answered "command not found"; the 224/225 mA readings belong to idle and were discarded.
- The Wi-Fi step never had a bridge: run.py ran `pt_off` and then `pt_wifi bridge`, which the firmware refuses with
  the rails off. Fixed: the bridge starts from `Run.boot(after_on=...)` and its reply is checked.
- `leakcam_kputest` (the KPU load) is not in the image.

### Status line before the 2026-10-29 cleanup

Status: **handover runs 1-2 done** (2026-10-25): USB -> cell under every camera-free load (1.06 A at 5 V), no power fault, K230 alive at 60.2 C afterwards; "no reset on the cell" still to prove; the BL616's USB console left the bus at the unplug (open). Planned since 2026-10-25 ( user, after test009: the cell carried K230 + both LED chains, ~1.15 A, on a USB
unplug with no brownout and no visible dimming).

### Handover run 2, 2026-10-25 12:53 (memtest with 24 MB reserved, `chip_temp` probe every 5 s)

| What | Result |
|---|---|
| USB input, every load on | 1.06 A at 5 V |
| Until the unplug (24 s) | 4 of 4 probes answered (39.8 -> 44.6 C), memtest passes 0-3 with 0 errors (89 MB), iperf ~28.7 Mbit/s |
| USB unplugged (USB-C 1 and USB-C 2 by the user; PR1 stayed in) | **the BL616's USB console on PR1 disappeared**; no console (no /dev/ttyACM*): nothing could be read or switched off; the LEDs stayed lit, the rails on (the runner's final `pt_off` found no port) |
| USB back, ~4 min later | the K230 answered `chip_temp`: **60.2 C** after ~5 min of full load (~3 min of it on the cell); then `pt_off` by hand |

- Run 1's "silent at 19 s" was the same thing: the console went at the unplug.
- Plugged back in while the K230 still ran: **no charging** until `pt_off` - bug-002 (the rails back-feed VUSB to
  ~2.2 V, a USB-C source waits for < 0.8 V before it turns VBUS on).
- Power: no fault in either run (rails on, LEDs lit, K230 answering afterwards).
- **Not proven yet: no K230 reset while on the cell** - a rebooted K230 also answers the probe, and lit LEDs prove
  nothing (an unconfigured PWM channel lights at full, test009).
- **Open: why the console went.** On battery with PR1 alone it works (every sleep test today); here the BL616 kept
  running (rails on) but its USB device left the bus at the unplug under ~1.3 A, and came back with USB-C 1. Next
  run: USB-C 2 out from the start, unplug only USB-C 1 (console survives -> USB-C 2 / D3 involved; drops -> the
  unplug transient on USB-C 1, e.g. a dip at the BL616's D+ pull-up); if it drops again, log on the board instead.

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

## Obstacles

- memory leak (mem_leak.py verdict in the Memory row): a leak fails the test (2026-10-30, automated run)

- + KPU change net in a loop: did not start: [W/LWP] not use fast load, out of memory 8388608 < 13193040 (2026-10-30, automated run)

- **The BL616's USB console (PR1) left the bus when USB-C 1 and USB-C 2 were unplugged under load** (2026-10-25,
  both runs; PR1 stayed in, the BL616 kept running) -> unexplained; next run separates USB-C 2 from the USB-C 1
  transient. The runner's final `pt_off` needs the console (a `finally` cannot reach the board without it).
- **The K230 went silent 19 s into handover run 1** (2026-10-25) -> memtest left the system no memory (109 of 128 MB,
  a program failed to load) -> memtest with 24 MB reserved, plus a `chip_temp` liveness probe every 5 s.
