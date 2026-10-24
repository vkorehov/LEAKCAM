# LEAKCAM rev 1 board tests

One folder per test, in bring-up order (001-099: one interface or chip; 100+: high level, the product's whole flow): setup, readings, result, and the obstacles met (problem -> cause -> fix),
with the test's own probe sheets and recordings. **Every test records battery energy** (section "Battery energy"): the
tested activity's average battery current and duration, and its energy per operation (uAh and mJ), measured with a
meter in the USB-C 1 cable's VBUS (only power path, cell charged, x VBAT: [test102 usb_meter.png](test102-session-current/usb_meter.png))
or, for sleep currents, in the battery lead; a shunt and scope for short events; "n/a"
with the reason where the battery is not involved. The figures feed [ENERGY.txt](../ENERGY.txt). **Every test records boot time** too (section "Boot time"): what the tested part adds to
the K230's power-on -> READY (its driver init, mount, probe, load) or to the BL616's wake path, from test022's
timestamped milestones; "n/a" where the part is not touched at boot. Energy and boot time are measured per part, in
the low-level tests, so a bottleneck shows where it is; the 100+ tests only add them up. **Every test records temperature** too: the K230 die (msh `chip_temp`) and the board (BL616 AHT20) at the start, peak and end of the activity. Board bugs: [bugs/](../bugs/bugs.txt). Rev 2: [bugs/v2.txt](../bugs/v2.txt).
Test tools (rail checks, probe-sheet renderer, BL616 test firmware, k230_hello): [tools/power-bringup](../tools/power-bringup/README.md).


**Every build is the minimal image, speed first, in every test** (2026-10-22, a standing practice, not a test of
its own): only the features some test's `## Needs` names, but none removed that makes anything slower, and the
fastest flags; every test also runs a heavily optimised build (`-O3`, LTO, `-march`/`-mtune`) side by side; each test records its image sizes and compile flags. After an
optimisation step the tests whose part changed run again. test008 finds the floor (the fewest bits that boot);
each test then builds its `## Needs` on that floor, so its Metrics show what its features cost.

**Every test README has the same structure** (2026-10-21), in this order: title, `Status: **<result>** (<date>).`,
a short introduction, then `## Setup` (hookup, image, probes), `## Needs` (the build features and dependencies the
test uses: kernel options, drivers, SDK components, libraries; their union is what the minimal image keeps), `## Steps`, `## Pass` (the criterion), `## Results`
(dated runs as `###` subsections), `## Metrics` (the full block of skill leakcam-test-metrics with its `Probes:`
line, every value at its maximum), `## Boot time`, `## Battery energy`, `## Obstacles` (problem -> cause -> fix,
written when it happens). Nothing is left out: an empty part says so ("Not run yet.", "None yet.", "not measured:
<reason>"). The status here and in the README's Status line say the same.

**Memory (user rule, 2026-10-24):** every run records memory on both chips (BL616 `mem`, K230 `free`: the Metrics
row "Memory BL616 / K230"), and every operation a test repeats is a leak test: a snapshot before and after each cycle,
`tools/power-bringup/mem_leak.py report <log>`, a leak fails the test (skill leakcam-test-metrics 5b). The leak test
cases, one per operation the product repeats:

| Operation repeated | Leak test | Cycles |
|---|---|---|
| Wi-Fi scan / leave + join over the bridge | test100 (`wloop.py`: 20 scans, 8 joins) | >= 8 |
| K230 session (power on, agent, power off) | test036 (`pt_session` loop) | >= 8 |
| NAND power cut + remount | test028 (`plt_loop.py`, K230 `free` per cycle) | >= 8 |
| DDR stress pass | test025 (memtest passes, K230 `free`) | >= 8 |
| HBN sleep / wake | test021 / test034 (BL616 `mem` after each wake: HBN RAM and the heap restart clean) | >= 8 |
| The detector itself | `tools/power-bringup/test_mem_leak.py` (stable, noise, warm-up, steady leak, deleted tasks, blocks, single drop) | - |

| Test | Date | Result |
|---|---|---|
| [test001 power and charging](test001-power-and-charging/README.md) | 2026-10-17/20 | pass: battery rails; charging with the bug-001 47 k |
| [test002 BL616 flash and console](test002-bl616-flash-and-console/README.md) | 2026-10-17 | pass |
| [test003 K230 rails and USB boot](test003-k230-rails-and-usb-boot/README.md) | 2026-10-17 | pass; USB-C attach with rails on: bug-002 |
| [test004 K230 hello from SRAM](test004-k230-hello-from-sram/README.md) | 2026-10-17 | pass |
| [test005 K230 NAND flashing](test005-k230-nand-flashing/README.md) | 2026-10-17 | pass |
| [test006 K230 NAND boot (SPL)](test006-k230-nand-boot-spl/README.md) | 2026-10-17 | pass |
| [test007 RT-Smart boot](test007-rtsmart-boot/README.md) | 2026-10-18/20 | pass; PMU RTC with R45 bridged (bug-003) |
| [test008 minimal image: the fewest bits that boot and pass](test008-minimal-image/README.md) | | planned: run first after board bring-up (after test007): the floor the feature tests build on |
| [test009 LED chains](test009-led-chains/README.md) | 2026-10-16/18 | pass |
| [test010 K230 <-> BL616 UART link](test010-uart-link/README.md) | 2026-10-18 | pass |
| [test011 NAND persistence, full disk, speed](test011-nand-fill-speed/README.md) | 2026-10-18 | pass |
| [test012 microphone](test012-microphone/README.md) | 2026-10-18 | pass |
| [test013 KPU change net](test013-kpu-change-net/README.md) | 2026-10-18/19 | pass: int8 net, 0.5 ms per inference, threshold 0.43 |
| [test014 SDIO wiring](test014-sdio-wiring/README.md) | 2026-10-19 | pass: CLK, CMD, D0-D3 both directions, no shorts |
| [test015 SDIO speed](test015-sdio-speed/README.md) | 2026-10-21 | pass: 50 MHz 4-bit, bus 21 MB/s, end to end 9.4 MB/s, no CRC errors; soak 56 runs / 109 MB clean |
| [test016 DDR bandwidth](test016-ddr-bandwidth/README.md) | | pass at 2667: 4.05/4.08 GB/s, CPU 1600 MHz |
| [test017 NAND raw speed and SPI clock](test017-nand-raw-speed/README.md) | | 80 MHz adopted (20.6 MB/s), 100 MHz: no sample window |
| [test018 UART link speed and latency](test018-uart-link-speed/README.md) | 2026-10-22 | pass: 4 Mbaud adopted, one rate from boot (10 MB, 0 errors, 353 KB/s); 6 Mbaud fails; round trip 1.48 ms -> 54 us |
| [test019 USB boot and flashing speed](test019-usb-flash-speed/README.md) | 2026-10-22 | pass: K230 image 26 s (erase 8 s, 5.6 MB/s, USB HS), BL616 4 s flash, no EN tap; ~45 s per board |
| [test020 camera CSI and I2C speed](test020-camera-csi-i2c/README.md) | | skipped: cameras not delivered (user, 2026-10-22: skip camera tests) |
| [test021 BL616 internal timing](test021-bl616-internal-timing/README.md) | 2026-10-22 | pass: wake -> main 12-16 ms, 3V3 1.7 ms, AHT20 50 -> 38 ms, ADC 3.1 -> 2.6 ms; ADC calibration 28.6 ms per wake (fix proposed); HBN RAM kept |
| [test022 K230 boot time](test022-boot-time/README.md) | | 0.506 s to app init (NAND 80 MHz, small /bin + /sdcard, one ISP set); mounts 0.22 s |
| [test023 battery ADC](test023-battery-adc/README.md) | 2026-10-21 | pass 4.07-4.37 V: reads 1.3 % high, calibrated (full scale 5329 mV, 64-read mean); low cell and load open |
| [test024 K230 heartbeat (K230_ALIVE)](test024-heartbeat/README.md) | 2026-10-21 | pass: 490 ms half-period (1.02 Hz), first edge 0.98 s after reset release, stops with the agent, low with rails off |
| [test025 DDR stress](test025-ddr-stress/README.md) | 2026-10-21 | 0 errors: 109 of 128 MB, 61 passes (5 min + 5 cold starts); 1 h and loaded runs open |
| [test026 K230 temperature](test026-temperature/README.md) | 2026-10-21 | open board: die 48.5 C idle (board 39 C), 53.3 C after 2 min memtest; KPU/encoder/enclosure open |
| [test027 H.265 encoder, synthetic frames](test027-video-encoder/README.md) | 2026-10-21 | pass: 2 x 15 fps at 510 kbit/s, +15 mA; top 83 fps at 1280x960; decode on PC open |
| [test028 NAND power loss](test028-nand-power-loss/README.md) | 2026-10-23 | **pass**: 500 random cuts (100 + 400), 0 lost/corrupt files; 4 check results unparsed (no loss, cause open) |
| [test029 camera buses idle](test029-camera-bus-idle/README.md) | 2026-10-21 | pass: I2C buses scan clean and idle high, CAM GPIOs low |
| [test030 cameras: detection, capture, image](test030-camera-capture/README.md) | | skipped: cameras not delivered (user, 2026-10-22: skip camera tests) |
| [test031 sleep and minimal currents](test031-sleep-current/README.md) | 2026-10-22 | RTC wake fixed (HBN pin pulls off + wait for Y3), 5/5 wakes; 1.23 mA asleep through USB; battery figure open |
| [test032 RTC wake](test032-rtc-wake/README.md) | 2026-10-22 | wake works on Y3 (5/5); Y3 -110 ppm open |
| [test033 every BL616 pin: function, levels, back-feed](test033-bl616-pins/README.md) | 2026-10-22 | pass: all pins as the table in 5 states (pt_pins + pin_check.py); product BOOT1 (IO27) fix; back-feed none |
| [test034 leak probe wake](test034-leak-probe-wake/README.md) | 2026-10-22 | pass with opens: product threshold fixed (was 1.65 V = dry, now 0.85 V); S1-S3 detected; HBN wake wet and dry works; water types, sweep, 8 h dry open |
| [test035 USB plug wake](test035-usb-plug-wake/README.md) | 2026-10-22 | pass: plug/unplug seen, HBN wake both edges, 19 cycles; in-session plug gets no power from a USB-C host (bug-002), product now waits for it and enters USB mode |
| [test036 K230 hang recovery](test036-k230-hang-recovery/README.md) | 2026-10-22 | pass: hang ends in 2.3 s, no-boot at the timeout; a restart is invisible to the heartbeat -> product session backstop 15 min + restart limit, boot timeout 90 -> 10 s |
| [test037 K230 PMU RTC](test037-k230-rtc/README.md) | 2026-10-21 | step 0 pass: runs from Y1 (+25 ppm vs 27 MHz), UTC fixed; handover open |
| [test038 battery brown-out below 3.10 V](test038-battery-brownout/README.md) | 2026-10-23 | **pass**: K230 VBAT cut -> rails off -> lockout HBN -> USB plug wakes (no EN); lockout skipped when USB is in; BL616 watchdog (12.9 s recovery); below 3 V needs a lab supply |
| [test039 K230 console over the BL616 link](test039-k230-console-over-link/README.md) | 2026-10-23 | pass: SPL, OpenSBI, kernel, msh, agent on /dev/ttyACM1 (4 Mbaud from the first line, 0 garbage), typing works, frames split; boot to app init 0.484 -> 0.30 s |
| **High level (100+): the product's whole flow** | | |
| [test100 Wi-Fi throughput end to end](test100-wifi-throughput/README.md) | 2026-10-23 | steps 1-3 pass: BL616 alone (join 3.8 s, joined 80 mA), K230 over the NetHub bridge (scan, join, DHCP, ping 8.8.8.8); BL616 hang in the event callback fixed; open: join 10 s scan wait, throughput (step 4) |
| [test101 session length](test101-boot-and-session-time/README.md) | | planned |
| [test102 session current](test102-session-current/README.md) | | planned |
| [test103 leak pipeline timing](test103-leak-pipeline-timing/README.md) | | later: after the cameras, Wi-Fi and the product firmware |
| [test104 battery energy audit](test104-battery-energy-audit/README.md) | | planned |

## Status by subsystem (2026-10-21)

| Subsystem | State | Test |
|---|---|---|
| Battery rails, 3V3_SLEEP | pass | test001 |
| Charging | pass with the bug-001 47 k and the cell wired to C11: full 0.89 A measured (end of charge to time); the earlier ~0.2-0.3 A / ~6 h were a wrong estimate and an unwatched night | test001 |
| USB-C power with K230 on | **open**: bug-002, plug USB before `pt_on` | test003 |
| BL616 flash, USB console, AHT20, leak ADC (dry) | pass | test002 |
| K230 rails, boot ROM, debug UART, code from SRAM | pass | test003, test004 |
| NAND flash + boot (SPL, DDR 2667, OpenSBI, RT-Smart), UFFS | pass | test005-007 |
| K230 PMU / RTC | pass: R45 bridged (bug-003), PMU RTC in use | test007 |
| White + IR LED chains | pass | test009 |
| K230 <-> BL616 UART link | pass | test010 |
| NAND persistence, full disk, speed | pass: read 9.4 MB/s (shell) / 12.7 MB/s (user program), write 1.8 MB/s | test011 |
| Microphone | pass: -7 dBFS claps over a -42 dBFS floor at 54 dB | test012 |
| KPU (change net) | pass: int8 on the KPU, 4.4 ms per check, same decisions as TensorFlow (int16 ran 40 layers on the CPU: 6.3 s) | test013 |
| Cameras | not tested: not delivered | test030 |
| SDIO wiring K230 <-> BL616 | pass | test014 |
| SDIO speed (synthetic, SDK echo device) | pass: 50 MHz 4-bit (max), bus writes 21.4 / reads 21.5 MB/s, end to end 9.4 MB/s; soak 109 MB, 0 errors | test015 |
| Battery ADC (K230 ADC_1) | pass 4.07-4.37 V: +1.3 %, calibrated in the agent (full scale 5329 mV, 64-read mean); low cell open | test023 |
| K230 heartbeat (K230_ALIVE) | pass: 490 ms half-period, stops with the agent, low with rails off | test024 |
| DDR 2667 MT/s stability | 0 errors over 61 passes and 5 retrainings; 1 h run open | test025 |
| Product BL616 firmware | **open**: no USB console on rev 1 (hangs before USB starts) | test015 |
| Leak probes wet, Wi-Fi through the BL616 | not tested: needs the product BL616 firmware | |

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: /etc/udev/rules.d/99-leakcam.rules opens them on every plug. Consoles: `/dev/ttyACM0` BL616 shell,
  `/dev/ttyACM1` K230 console (over the link UART since 2026-10-22, from the first SPL line; the CH340X `/dev/ttyUSB0`
  only for the BootROM).
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
- Audio played inside the VM stutters: judge recordings by their data or play them on the Mac.
