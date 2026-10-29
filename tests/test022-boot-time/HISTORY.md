# test022: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Status line before the 2026-10-29 cleanup

Status: **0.30 s** reset release -> app init (2026-10-23: the console on UART1 at 4 Mbaud from SPL, tests/test039; 0.506 s on 2026-10-21, 1.00 s before); msh prompt and READY not timed yet.

### Automated run 2026-10-25 (run.py, 170 s): pass

8 of 8 cold boots reached msh with `boot_times`: reset release -> app init 0.320 / 0.320 / 0.321 s (min / median / max); mounts done at 0.311-0.312 s.

### Results (2026-10-21)

The kernel times itself: `rdtime` (27 MHz) counts from the K230's reset release (a crash reboot restarts it too), and
temporary init hooks record it at the end of each RT-Thread init level (`boot_times`,
[leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c)). The CH340 console attaches too late for the first lines
(it is powered from the switched 3V3, bug-002), so no host timing is needed.

| Step (after reset release) | Boot 1 (cold) | Boot 2 (crash reboot) | Step |
|---|---|---|---|
| ROM, SPL, DDR training, OpenSBI -> kernel init | 0.230 s | 0.234 s | 230 ms |
| board, prev, device, component init | 0.285 s | 0.278 s | 50 ms |
| **env init: /bin + /sdcard UFFS mounts** | **0.980 s** | **0.973 s** | **695 ms** |
| app init done | 1.003 s | 0.997 s | 23 ms |
| msh prompt, agent READY | not timed | | |

NAND at 80 MHz (test017), 11 cold boots read: kernel init 0.207 s (-23 ms: SPL/U-Boot load faster), board..component
0.262 s, **mounts done 0.853-0.863 s (0.59-0.60 s, -100 ms)**, **app init 0.876-0.886 s (-12 %)**.

**The UFFS mounts are 70 % of the boot to app init**: first lever. Then the NAND clock (80 MHz, test017) for the
mount reads and the kernel load. Still to time: main/msh and the agent's READY (needs the product BL616 or a K230-side
hook), and 5 cold boots (each costs a VMware click for the CH340).

### Superseded text (2026-10-21)

- Purpose: **MISSING**: the test firmware does not time it and the product BL616 firmware (which logs "READY <ms> after
  power-on") does not run on rev 1 yet.
- Target: Estimate before the first measurement (2026-10-21): 1-3.5 s power-on to READY.

### Metrics setup (2026-10-21, bench image)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

### Superseded metric values

- 2026-10-21: Boot, reset release -> app init: 0.88 s at 80 MHz (11 boots, 0.876-0.886); 1.00 s at 50 MHz; longest
  step: UFFS mounts 0.59 s.
- 2026-10-21: NAND clock in the boot: 80 MHz (since 2026-10-21).

## Obstacles

(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **Host-side boot timing misses the start** -> the CH340 (U2) is powered from the switched 3V3 (bug-002), so it
  re-enumerates on every cold boot and appears in the VM ~10 s after power-on -> the kernel times itself (`rdtime`
  from reset release, `boot_times`); the skill's `boot_time.py` is only for a console that stays up.
- **Console reader stopped early** -> the agent prints `READY not answered` again and again (the test BL616 firmware
  does not answer) -> read for a fixed time, not up to the first prompt; 7 of 18 boots were not read for that reason,
  all booted.
- **The kernel crashed in `ddr_bench 16384`** (test016) and rebooted: that warm reboot restarts `rdtime` too, so it
  gave a second sample.
- **/sdcard 176 -> 32 MiB (2026-10-21, image with test028's writer):** mounts 0.374 s (was 0.59), app init **0.660 s**
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
