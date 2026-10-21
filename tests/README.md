# LEAKCAM rev 1 board tests

One folder per test, in bring-up order (001-099: one interface or chip; 100+: high level, the product's whole flow): setup, readings, result, and the obstacles met (problem -> cause -> fix),
with the test's own probe sheets and recordings. **Every test records battery energy** (section "Battery energy"): the
tested activity's average battery current and duration, and its energy per operation (uAh and mJ), measured with a
meter in the USB-C 2 cable's VBUS (only power path, cell charged, x VBAT: [test102 usb_meter.png](test102-session-current/usb_meter.png))
or, for sleep currents, in the battery lead; a shunt and scope for short events; "n/a"
with the reason where the battery is not involved. The figures feed [ENERGY.txt](../ENERGY.txt). **Every test records boot time** too (section "Boot time"): what the tested part adds to
the K230's power-on -> READY (its driver init, mount, probe, load) or to the BL616's wake path, from test021's
timestamped milestones; "n/a" where the part is not touched at boot. Energy and boot time are measured per part, in
the low-level tests, so a bottleneck shows where it is; the 100+ tests only add them up. **Every test records temperature** too: the K230 die (msh `chip_temp`) and the board (BL616 AHT20) at the start, peak and end of the activity. Board bugs: [bugs/](../bugs/bugs.txt). Rev 2: [bugs/v2.txt](../bugs/v2.txt).
Test tools (rail checks, probe-sheet renderer, BL616 test firmware, k230_hello): [tools/power-bringup](../tools/power-bringup/README.md).

| Test | Date | Result |
|---|---|---|
| [test001 power and charging](test001-power-and-charging/README.md) | 2026-10-17/20 | pass: battery rails; charging with the bug-001 47 k |
| [test002 BL616 flash and console](test002-bl616-flash-and-console/README.md) | 2026-10-17 | pass |
| [test003 K230 rails and USB boot](test003-k230-rails-and-usb-boot/README.md) | 2026-10-17 | pass; USB-C attach with rails on: bug-002 |
| [test004 K230 hello from SRAM](test004-k230-hello-from-sram/README.md) | 2026-10-17 | pass |
| [test005 K230 NAND flashing](test005-k230-nand-flashing/README.md) | 2026-10-17 | pass |
| [test006 K230 NAND boot (SPL)](test006-k230-nand-boot-spl/README.md) | 2026-10-17 | pass |
| [test007 RT-Smart boot](test007-rtsmart-boot/README.md) | 2026-10-18/20 | pass; PMU RTC with R45 bridged (bug-003) |
| [test008 LED chains](test008-led-chains/README.md) | 2026-10-16/18 | pass |
| [test009 K230 <-> BL616 UART link](test009-uart-link/README.md) | 2026-10-18 | pass |
| [test010 NAND persistence, full disk, speed](test010-nand-fill-speed/README.md) | 2026-10-18 | pass |
| [test011 microphone](test011-microphone/README.md) | 2026-10-18 | pass |
| [test012 KPU change net](test012-kpu-change-net/README.md) | 2026-10-18/19 | pass: int8 net, 0.5 ms per inference, threshold 0.43 |
| [test013 SDIO wiring](test013-sdio-wiring/README.md) | 2026-10-19 | pass: CLK, CMD, D0-D3 both directions, no shorts |
| [test014 SDIO speed](test014-sdio-speed/README.md) | 2026-10-21 | pass: 50 MHz 4-bit, bus 21 MB/s, end to end 9.4 MB/s, no CRC errors; soak 56 runs / 109 MB clean |
| [test015 DDR bandwidth](test015-ddr-bandwidth/README.md) | | pass at 2667: 4.05/4.08 GB/s, CPU 1600 MHz |
| [test016 NAND raw speed and SPI clock](test016-nand-raw-speed/README.md) | | 80 MHz adopted (20.6 MB/s), 100 MHz: no sample window |
| [test017 UART link speed and latency](test017-uart-link-speed/README.md) | | planned |
| [test018 USB boot and flashing speed](test018-usb-flash-speed/README.md) | | planned |
| [test019 camera CSI and I2C speed](test019-camera-csi-i2c/README.md) | | planned |
| [test020 BL616 internal timing](test020-bl616-internal-timing/README.md) | | planned |
| [test021 K230 boot time](test021-boot-time/README.md) | | 0.506 s to app init (NAND 80 MHz, small /bin + /sdcard, one ISP set); mounts 0.22 s |
| [test022 battery ADC](test022-battery-adc/README.md) | 2026-10-21 | pass 4.07-4.37 V: reads 1.3 % high, calibrated (full scale 5329 mV, 64-read mean); low cell and load open |
| [test023 K230 heartbeat (K230_ALIVE)](test023-heartbeat/README.md) | 2026-10-21 | pass: 490 ms half-period (1.02 Hz), first edge 0.98 s after reset release, stops with the agent, low with rails off |
| [test024 DDR stress](test024-ddr-stress/README.md) | 2026-10-21 | 0 errors: 109 of 128 MB, 61 passes (5 min + 5 cold starts); 1 h and loaded runs open |
| [test025 K230 temperature](test025-temperature/README.md) | 2026-10-21 | open board: die 48.5 C idle (board 39 C), 53.3 C after 2 min memtest; KPU/encoder/enclosure open |
| [test026 H.265 encoder, synthetic frames](test026-video-encoder/README.md) | 2026-10-21 | pass: 2 x 15 fps at 510 kbit/s, +15 mA; top 83 fps at 1280x960; decode on PC open |
| [test027 NAND power loss](test027-nand-power-loss/README.md) | 2026-10-21 | short run pass: 13 random cuts, 0 lost/corrupt files; 100-cut run open |
| [test028 camera buses idle](test028-camera-bus-idle/README.md) | | planned |
| [test029 cameras: detection, capture, image](test029-camera-capture/README.md) | | placeholder: cameras not delivered |
| [test030 sleep and minimal currents](test030-sleep-current/README.md) | | planned |
| [test031 RTC wake](test031-rtc-wake/README.md) | | planned |
| [test032 every BL616 pin: function, levels, back-feed](test032-bl616-pins/README.md) | | planned |
| [test033 leak probe wake](test033-leak-probe-wake/README.md) | | planned |
| [test034 USB plug wake](test034-usb-plug-wake/README.md) | | planned |
| [test035 K230 hang recovery](test035-k230-hang-recovery/README.md) | | planned |
| [test036 K230 PMU RTC](test036-k230-rtc/README.md) | | planned |
| [test037 battery brown-out below 3.10 V](test037-battery-brownout/README.md) | | planned |
| **High level (100+): the product's whole flow** | | |
| [test100 Wi-Fi throughput end to end](test100-wifi-throughput/README.md) | | planned |
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
| White + IR LED chains | pass | test008 |
| K230 <-> BL616 UART link | pass | test009 |
| NAND persistence, full disk, speed | pass: read 9.4 MB/s (shell) / 12.7 MB/s (user program), write 1.8 MB/s | test010 |
| Microphone | pass: -7 dBFS claps over a -42 dBFS floor at 54 dB | test011 |
| KPU (change net) | pass: int8 on the KPU, 4.4 ms per check, same decisions as TensorFlow (int16 ran 40 layers on the CPU: 6.3 s) | test012 |
| Cameras | not tested: not delivered | test029 |
| SDIO wiring K230 <-> BL616 | pass | test013 |
| SDIO speed (synthetic, SDK echo device) | pass: 50 MHz 4-bit (max), bus writes 21.4 / reads 21.5 MB/s, end to end 9.4 MB/s; soak 109 MB, 0 errors | test014 |
| Battery ADC (K230 ADC_1) | pass 4.07-4.37 V: +1.3 %, calibrated in the agent (full scale 5329 mV, 64-read mean); low cell open | test022 |
| K230 heartbeat (K230_ALIVE) | pass: 490 ms half-period, stops with the agent, low with rails off | test023 |
| DDR 2667 MT/s stability | 0 errors over 61 passes and 5 retrainings; 1 h run open | test024 |
| Product BL616 firmware | **open**: no USB console on rev 1 (hangs before USB starts) | test014 |
| Leak probes wet, Wi-Fi through the BL616 | not tested: needs the product BL616 firmware | |

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: `sudo setfacl -m u:$USER:rw /dev/ttyACM0 /dev/ttyUSB0` after each re-enumeration.
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
- Audio played inside the VM stutters: judge recordings by their data or play them on the Mac.
