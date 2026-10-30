# test008: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-30 (run.py, 71 s): pass

5 cold boots of the current image to msh; metrics from the harness.


Old status line (2026-10-22): **in progress** (2026-10-22): builds 1+2 (unused/inherited off) app init 0.506 -> 0.49 s; kernel -O2 not adopted (two latent bugs, one fixed); programs' flags next. **Runs first after board bring-up** (after test007, the first RT-Smart boot): the
floor every later test builds its `## Needs` on.

### Flags B on the board, 2026-10-23 21:05 (leakcam_cpubench, 20 runs, median ms; check line identical in all six)

| build | reduce x2 | compare | compress key | crc32 | sum |
|---|---|---|---|---|---|
| -Os (current) | 21.07 | 7.57 | 279.73 | 36.17 | 344.5 |
| -Os LTO | 19.35 | 7.47 | 281.15 | 35.41 | 343.4 |
| -O2 | 32.74 | 4.95 | 272.37 | 35.59 | 345.7 |
| -O2 LTO | 29.26 | 5.20 | 273.64 | 33.91 | 342.0 |
| -O3 | 19.49 | 4.67 | 272.39 | 28.49 | 325.0 |
| **-O3 LTO** | **6.26** | 4.79 | 275.03 | 28.49 | **314.6** |

-O3 LTO is the fastest (-9 % on the sum, reduce 3.4x faster: vectorised with `-march=rv64imafdcv`), +11.7 KB on
leakcam_wake, +127 KB on leakcam_stream. Rule (speed first): **adopt -O3 -flto** for the programs; the compress
step (miniz level 3 on a 1.2 MB frame, 275 ms) dominates and is the next lever. Not adopted yet: the product
programs are rebuilt with the firmware rewrite.

### Flags B, 2026-10-23: the programs at -Os / -O2 / -O3, each with and without LTO (sizes; speed open)

`LEAKCAM_OPT` / `LEAKCAM_LTO` in firmware/k230_capture/rtsmart/Makefile (defaults `-Os`, no LTO), six builds (56-97 s
each), stripped sizes in bytes:

| build | leakcam_wake | leakcam_stream | leakcam_hist | leakcam_agent | leakcam_cpubench |
|---|---|---|---|---|---|
| -Os | 5 181 000 | 1 030 880 | 100 376 | 87 552 | 75 376 |
| -Os LTO | 5 180 488 | 1 022 592 | 96 280 | 87 536 | 75 376 |
| -O2 | 5 185 096 | 1 067 656 | 100 376 | 91 648 | 79 472 |
| -O2 LTO | 5 184 584 | 1 079 856 | 100 376 | 91 632 | 75 376 |
| -O3 | 5 189 104 | 1 129 096 | 104 472 | 91 648 | 83 568 |
| -O3 LTO | 5 192 688 | 1 157 712 | 100 376 | 91 632 | 83 568 |

The flags move leakcam_wake by 0.2 % at most: nncase and MPP are most of it (the map file is the lever, step 3).
Speed decides (rule): [leakcam_cpubench.c](leakcam_cpubench.c) times the wake path's CPU steps on a fixed synthetic
frame pair (reduce x2, compare, keyframe compress2 level 3, CRC; not the KPU net) and prints a check line so every
build is shown to compute the same. Host (x86, -O2): reduce 2.3 ms, compare 0.38 ms, compress 36.7 ms, CRC 13.1 ms.
A test image with all six variants (`LEAKCAM_BENCH=1` per variant, `LEAKCAM_BENCH_DIR` copies them in) and test025's
memtest is built (kdimg 17:47); flashed 2026-10-23 21:03 and run: the board results are "Flags B on the board" below.

### Flags A, 2026-10-22: kernel -O2 (instead of the SDK's -O1) - not adopted, back to -O1 (user: safe side)

rtthread.bin at -O2: 3 724 328 B (+12.4 KB). On the board:
1. **Every boot faulted in gnne_device_init** (Store/AMO page fault at 0x91301d8c): Canaan's
   gnne_setup_kpu_bandwidth() writes the KPU bandwidth registers at their physical address, with the rt_ioremap
   version disabled by `#if 0`; at -O1 it happened to work. **Fixed** (install.sh: the mapped version) and kept at
   -O1 too: a real bug.
2. **Then leakcam_agent faulted** (Instruction Page Fault at 0xfffffffffffff000, ra the same): the user process
   returns to the signal-return trampoline page, which at -O2 is not mapped/set up: a second latent bug in the
   kernel's LWP signal path (likely an inline-asm/volatile assumption). Open.
Kernel back at -O1 (install.sh KOPT) with the KPU fix: 3 712 016 B, boots, app init 0.484-0.493 s (3 boots), no
fault. Whether -O2 is faster at all was not measured (it did not run); the kernel's share of the boot without the
mounts is ~0.26 s. Next lever: the programs (leakcam_wake 5.18 MB: sections, LTO, -O2/-O3), where the compute time
of a session goes.

The kernel's bulk is in what the product needs (MPP, GNNE/KPU, lwIP, the WLAN framework): the larger levers are
the flags (kernel -O1 -g; programs without sections/LTO) and leakcam_wake (5.18 MB).

**Build 1 + 2 on the board (2026-10-22, flashed 05:2x):** boots; 3 cold boots app init **0.484 / 0.494 / 0.493 s**
(was 0.506; mounts 0.20-0.21 s instead of 0.22: 147 KB less on /sdcard); ADC, I2C (both buses clean), RTC, NAND
80 MHz (20.6 MB/s, 0 errors), pads all as before; die 42.5 C; flash write 205 mA.

### Build 2, 2026-10-22: inherited from the CanMV board, unused here

Off: `MPP_ENABLE_MIDDLEWARE_LIB_FFMPEG` (nothing of ours links libav*: streaming is rtmp.c + Opus) and
`MPP_DSI_ENABLE_VIRT` (virtual display; no display on LEAKCAM). rtthread.bin 3 711 904 B (-248 B), programs and
kdimg unchanged: both were only built, never in the image - the gain is build time (FFmpeg). Reviewed, kept:
the MPP threads (auto_load, mcm_task, thermal_detect) and the boot messages "Can't find sensor on csi0/2", "vo delay
timer" come from the prebuilt MPP libraries (needed for cameras/encoder); etx/erx (lwIP netif of the Wi-Fi bridge),
mmcsd_detect (SDIO to the BL616), the GPIO threads. To check: MPP_ISP_CONFIG_INSTALL_BIN and _JSON_XML both on (the
ISP set twice in /bin; with the cameras, test020/030); FAST_BOOT_CONFIGURATION packs one program into the image
(rtapp.elf.gz, CanMV's MicroPython) - a speed candidate for leakcam_agent/wake, **MISSING** later.

### Build 1, 2026-10-22: defaults no test needs, off (speed first: none of them speeds anything up)

Off: VG_LITE, UART2/3/4, I2C_BITOPS, SAL AF_UNIX, LWIP IGMP / RAW / NETIF_LOOPBACK / USING_PING, NETDEV
ifconfig/ping/netstat, WLAN MSH_CMD / AUTO_CONNECT / CFG / SCAN_SORT, OVERFLOW_CHECK; the temporary test programs
memtest and venctest out of the image (SDK copy: moved to src/applications_off). Kept because the SDK itself
references them: RT_DEBUG (44 RT_ASSERT users), RT_USING_PROC + DFS_PROCFS (17 drivers register /proc entries),
RT_USING_USAGE (sys_cpu_usage).

| | Baseline | Build 1 | Difference |
|---|---|---|---|
| rtthread.bin | 3 749 752 B | 3 712 152 B | -37.6 KB (-1.0 %) |
| /sdcard programs | 6 763 034 B | 6 616 314 B | -147 KB (memtest, venctest) |
| kdimg | 45.41 MB | 45.38 MB | |
| options =y | 145 | 127 | -18 |
| boot to app init | 0.506 s | 0.484-0.494 s (builds 1+2) | -12..-22 ms |

### Baseline 2026-10-22

| Part | Size | Note |
|---|---|---|
| kdimg | 45.4 MB | /sdcard 32 MiB, /bin 8 MiB |
| rtthread.bin (kernel) | 3.75 MB | `-O1 -g`, 145 options =y |
| /bin | 0.68 MB | one ISP set (1280x960) |
| /sdcard programs | 6.76 MB | leakcam_wake 5.18 MB, stream 1.03, kmodel 0.21, hist 0.10, agent 0.09, venctest 0.08 (test) |
| programs' flags | `-Os`, `-ffunction-sections -fdata-sections`, `--gc-sections`, strip | no LTO, `-Os` not `-O2`/`-O3` |
| boot to app init | 0.506 s | test022 |
| BL616 product / test firmware | 1.27 MB (2026-10-21) / 121 KB | `-Os -g`, sections, `--gc-sections`, `-flto` |

### Superseded metric values

- Image (sizes): "baseline above"; Compile flags: "baseline above" (the Baseline 2026-10-22 table).

## Obstacles

- **Unattended flash refused by VMware** (2026-10-23 17:47): `pt_boot1 high`, `pt_on`, `pt_run`, but 29f1:0230 did not
  reach Linux in 60 s (the BL616 ports pass through by themselves, the K230 boot device asks "connect to Linux").
  The runner stopped cleanly (rails off, BOOT1 low), the old image kept. Flash the bench image when someone is at
  the Mac.
- **A full SDK build wipes `images/sdcard/app`** and puts back only each app's own outputs: files copied there by hand
  are gone from the image. -> `LEAKCAM_BENCH_DIR` makes the leakcam Makefile copy the prebuilt variants itself.
- **Link failed after switching defaults off** (rt_assert_handler, rt_proc_entry_*, sys_cpu_usage undefined) -> SDK
  code needs RT_DEBUG, RT_USING_PROC + DFS_PROCFS and RT_USING_USAGE -> kept on.
- **Defconfig step failed** (Kconfig.app: can't open venctest/Kconfig) -> the app list is generated from the folders
  in src/applications; the other session's temporary venctest had no Kconfig -> memtest and venctest moved to
  src/applications_off (SDK copy).
- **Kernel -O2 faulted at every boot in gnne_device_init** -> the KPU driver writes 0x91301d8c physically (the
  ioremap branch `#if 0`) -> install.sh patch "LEAKCAM: KPU bandwidth mapped" (kept at -O1).
- **Kernel -O2: leakcam_agent faults returning to 0xfffffffffffff000** (signal-return trampoline) -> open; kernel
  back at -O1 (user: safe side).
- **"no K230 boot device after 90 s"** -> the VMware click came later -> wait up to 180 s before giving up. (Since
  2026-10-24 the popups are off and the device attaches in ~2 s: the runners wait 20 s, a miss is a board fault.)
