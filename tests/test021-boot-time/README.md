# test021: K230 boot time

Status: **0.88 s** reset release -> app init with NAND at 80 MHz (was 1.00 s at 50 MHz, 2026-10-21); msh prompt and READY not timed yet.

Every wake powers the K230 up from cold, so its boot time is paid on every session (the whole session: test101).
Not measured yet: the test firmware does not time it and the product BL616 firmware (which logs "READY <ms> after
power-on") does not run on rev 1 yet.

## Target

The design wants the fastest boot: it is paid on every wake (ENERGY.txt, the biggest lever). Estimate before the
first measurement (2026-10-21): 1-3.5 s power-on to READY. Target to set from the first measurement; first
candidates: sensor probes without cameras, the /sdcard UFFS mount, DDR training, the kernel load (NAND clock).

## Setup

BL616 test firmware; K230 NAND image (current test image or the product image); both consoles recorded with
timestamps (skill k230-rtsmart-hangs `console_record.py`, K230 console on the CH340; BL616 console on PR1).

## Steps

1. Rails off >= 5 s, then `pt_on` (the BL616 prints the 3V3 ramp), `pt_run` at t0 (reset released).
2. From the K230 console, the time of each milestone after t0:
   - U-Boot SPL banner (ROM read SPL from NAND);
   - "Firmware run has completed 2667" (DDR training done);
   - OpenSBI banner;
   - RT-Smart banner;
   - UFFS mounted (/bin, /sdcard), `msh />`;
   - agent `READY` on the link (the BL616's UART0 shows it with `pt_uart on`).
3. 5 cold boots: median and spread per step; name the longest step.
4. Repeat after each speed change that touches boot (NAND clock test016, DDR 3200 test024) to see what it bought.

## Pass

Recorded per step; the biggest steps named as candidates to shorten.


## Results (2026-10-21)

The kernel times itself: `rdtime` (27 MHz) counts from the K230's reset release (a crash reboot restarts it too), and
temporary init hooks record it at the end of each RT-Thread init level (`boot_times`,
[leakcam_bench.c](../test016-nand-raw-speed/leakcam_bench.c)). The CH340 console attaches too late for the first lines
(it is powered from the switched 3V3, bug-002), so no host timing is needed.

| Step (after reset release) | Boot 1 (cold) | Boot 2 (crash reboot) | Step |
|---|---|---|---|
| ROM, SPL, DDR training, OpenSBI -> kernel init | 0.230 s | 0.234 s | 230 ms |
| board, prev, device, component init | 0.285 s | 0.278 s | 50 ms |
| **env init: /bin + /sdcard UFFS mounts** | **0.980 s** | **0.973 s** | **695 ms** |
| app init done | 1.003 s | 0.997 s | 23 ms |
| msh prompt, agent READY | not timed | | |

NAND at 80 MHz (test016), 11 cold boots read: kernel init 0.207 s (-23 ms: SPL/U-Boot load faster), board..component
0.262 s, **mounts done 0.853-0.863 s (0.59-0.60 s, -100 ms)**, **app init 0.876-0.886 s (-12 %)**.

**The UFFS mounts are 70 % of the boot to app init**: first lever. Then the NAND clock (80 MHz, test016) for the
mount reads and the kernel load. Still to time: main/msh and the agent's READY (needs the product BL616 or a K230-side
hook), and 5 cold boots (each costs a VMware click for the CH340).

## Metrics (2026-10-21, bench image)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.88 s at 80 MHz (11 boots, 0.876-0.886); 1.00 s at 50 MHz | target: to set | longest step: UFFS mounts 0.59 s |
| This part's boot share | - | - | (the boot is this test) |
| Test run time | 1 command; setup: reflash (USB boot) ~5 min with clicks | - | |
| NAND clock in the boot | 80 MHz (since 2026-10-21) | 100 MHz has no window (test016) | |
| Current: rails in reset / boot peak / msh idle | 86.3 / ~227 / 204 mA | - | USB-C 1 VBUS |
| Power, energy per boot (to app init) | ~215 mA x 0.88 s = 0.72 J, 53 uAh (1.0 s: 0.82 J) | - | 3.8 V nominal |
| Per day | 4 report sessions: 0.24 mAh/day (boot to app init only) | ENERGY.txt 3 | 0.7 % of 1000 mAh / month |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

This test is the boot time measurement.

## Battery energy

Battery current through the boot, per step (scope over a shunt in the battery lead): energy per boot = the fixed cost of every wake. Also after each speed change (NAND clock, DDR 3200).

## Obstacles

- **Host-side boot timing misses the start** -> the CH340 (U2) is powered from the switched 3V3 (bug-002), so it
  re-enumerates on every cold boot and appears in the VM ~10 s after power-on -> the kernel times itself (`rdtime`
  from reset release, `boot_times`); the skill's `boot_time.py` is only for a console that stays up.
- **Console reader stopped early** -> the agent prints `READY not answered` again and again (the test BL616 firmware
  does not answer) -> read for a fixed time, not up to the first prompt; 7 of 18 boots were not read for that reason,
  all booted.
- **The kernel crashed in `ddr_bench 16384`** (test015) and rebooted: that warm reboot restarts `rdtime` too, so it
  gave a second sample.
- **/sdcard 176 -> 32 MiB (2026-10-21, image with test027's writer):** mounts 0.374 s (was 0.59), app init **0.660 s**
  (was 0.88). Less than the ~0.4 s estimated from block counts: used blocks cost more than empty ones; /bin
  (20 MiB, ~3.6 MB used) is now the larger share.
- **/bin 20 -> 8 MiB (2026-10-21):** mounts 0.355 s (0.374), app init **0.641 s**: only 19 ms for 96 fewer blocks.
  UFFS mount time goes with the **used pages**, not the partition size: it reads the tags of every page holding
  data. /bin 3.3 MB + /sdcard ~7 MB = ~10.5 MB = ~5300 pages x ~65 us = ~0.35 s, as measured. Levers now: less data
  (ISP tuning files for one resolution only, stripped programs) or /sdcard mounted only when a session needs it.
- **/bin trimmed to the 1280x960 ISP set (3.3 -> 0.68 MB, install.sh, 2026-10-21):** 3 cold boots read, all equal:
  component init 0.262 s, mounts done 0.483 s (0.22 s), **app init 0.506 s** (was 0.641; 1.00 s this morning).
  Temperature at msh idle after: die 45.8-46.7 C (chip_temp), board 37.4 C (AHT20); peak during the boot not
  measured (the boot is < 1 s, chip_temp needs the shell).
- **Build failed after limiting the ISP pattern to `ov5647-1280x960`** -> the SDK rule appends `.json`/`.xml` to the
  pattern for the rsync copy (`ov5647-*` only worked through the shell glob) -> pattern `ov5647-1280x960*`; check
  images/bin holds config.txt + the 4 files before flashing.

