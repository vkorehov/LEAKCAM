# LEAKCAM rev 1 board tests

One folder per test, in bring-up order (001-099: one interface or chip; 100+: the product's whole flow). The table
below is generated from each test's Status line, so the two always agree.

**Layout (user, 2026-10-29: "record only current findings"):**
- `testNNN-*/README.md`: only what is current - Status line, Setup, Needs, Steps, Pass, `## Result` (the findings
  that hold now, `Open:` items), Metrics (current values), Boot time, Battery energy.
- `testNNN-*/HISTORY.md`: earlier runs, superseded findings and every obstacle (problem -> cause -> fix), newest first.
- Lessons the product build must respect (chip quirks, SDK patches, orders, timing, values): skill
  `leakcam-product-build`, one rule per lesson with its test.
- This index's own history: [HISTORY.md](HISTORY.md).

**Standard:** `tools/test-standard/check_tests.py` checks every README's sections and the 14 metric rows of skill
leakcam-test-metrics (a value measured, `n/a: <reason>`, or `**MISSING**`); after a run `check_tests.py testNNN` must
pass before the run is reported. Every test records boot time, battery energy, temperature and memory on both chips;
every repeated operation is a leak test (`tools/power-bringup/mem_leak.py`). Tests run on the **test firmware only**
(the product firmware is rewritten after the tests), built with the SDK's default flags (-Os -g, no LTO) for fast builds; -O3 / LTO
only in tests about optimisation (user, 2026-10-29).

**Automated runs:** a test with a `run.py` runs unattended through PR1; `tools/test-standard/run_tests.py` runs them in
turn (output in the test's `run.log`). `tools/test-standard/harness.py` times the K230 cold boot, samples temperature,
VBAT and memory, fills only metric cells that still say MISSING (recorded readings, the user's meter readings above
all, are never overwritten) and appends the run and its obstacles to HISTORY.md; the README's Status and Result are
then rewritten from the run. Never touch the board while a run is going.

Leak tests, one per operation the product repeats:

| Operation repeated | Leak test | Cycles |
|---|---|---|
| Wi-Fi scan / leave + join over the bridge | test100 (`wloop2.py`: 20 scans, 8 joins, verdict from the BL616 counters) | >= 8 |
| K230 session (power on, agent, power off) | test036 (`pt_session` loop) | >= 8 |
| NAND power cut + remount | test028 (`plt_loop.py`, K230 `free` per cycle) | >= 8 |
| DDR stress pass | test025 (memtest passes, K230 `free`) | >= 8 |
| HBN sleep / wake | test021 / test034 (BL616 `mem` after each wake: HBN RAM and the heap restart clean) | >= 8 |
| The detector itself | `tools/power-bringup/test_mem_leak.py` (stable, noise, warm-up, steady leak, deleted tasks, blocks, single drop) | - |

| Test | Date | Result |
|---|---|---|
| [test001 power and charging](test001-power-and-charging/README.md) | 2026-10-25 | **pass**: battery rails, USB detect and charging (full 0.89 A, CV taper, termination) with the |
| [test002 BL616 flash and console (PR1)](test002-bl616-flash-and-console/README.md) | 2026-10-25 | **pass**: flash over PR1 and the USB CDC console work; `pt_status` reads leak node, AHT20 and USB. |
| [test003 K230 rails and USB boot](test003-k230-rails-and-usb-boot/README.md) | 2026-10-25 | **pass**: all K230 rails within limits, 3V3 up 1.69-1.70 ms after K230_PWR, the K230 USB boot |
| [test004 K230 hello from SRAM](test004-k230-hello-from-sram/README.md) | 2026-10-25 | **pass**: k230_hello loads over USB into SRAM in 4.2 s and runs (3 of 3 boots), K230_ALIVE toggles. |
| [test005 K230 NAND flashing](test005-k230-nand-flashing/README.md) | 2026-10-25 | **pass**: the full image flashes to NAND, re-flashes without the B1 short, and boots 5 of 5. |
| [test006 K230 NAND boot (SPL)](test006-k230-nand-boot-spl/README.md) | 2026-10-31 | **pass**: 5 of 5 cold boots from NAND (SPL, DDR training at 3200 MT/s all-ODT, OpenSBI, kernel, msh); reset |
| [test007 RT-Smart boot](test007-rtsmart-boot/README.md) | 2026-10-25 | **pass**: 5 of 5 cold boots from NAND to msh, /bin and /sdcard mounted, agent starts; PMU RTC |
| [test008 minimal image - the fewest bits that still boot and pass](test008-minimal-image/README.md) | 2026-10-23 | **in progress**: builds 1+2 adopted (app init 0.484-0.494 s); kernel stays -O1; programs |
| [test009 LED chains (white U14/J1, IR U15/J2)](test009-led-chains/README.md) | 2026-10-25 | **pass**: both chains light and reach full current at 100 % duty on the K230 PWM (white +0.34 A, |
| [test010 K230 <-> BL616 UART link](test010-uart-link/README.md) | 2026-10-31 | **pass**: 2 Mbaud, 8 boots, 112 READY frames with valid checksums, echo clean. |
| [test011 NAND persistence, full disk, speed](test011-nand-fill-speed/README.md) | 2026-10-25 | **pass**: files survive a power cycle, a full disk fails cleanly, read 14.4 MB/s, write 2.1 MB/s. |
| [test012 microphone U13 (MSM381ACB026) on the K230 inner codec](test012-microphone/README.md) | 2026-10-25 | **pass**: 3 x 10 s recordings, a level every second, rms -25.3..-2.8 dBFS; no dropouts, no |
| [test013 KPU, change net (leakcam_change.kmodel)](test013-kpu-change-net/README.md) | 2026-10-25 | **pass**: int8 net whole on the KPU, 0.5 ms per inference, one distance 2.9 ms median, output |
| [test014 SDIO wiring, K230 MMC0 <-> BL616 SDIO](test014-sdio-wiring/README.md) | 2026-10-25 | **pass**: CLK, CMD, D0-D3 connected both directions, none stuck, no shorts; CLK and CMD toggle |
| [test015 SDIO speed, K230 MMC0 <-> BL616 SDIO device](test015-sdio-speed/README.md) | 2026-10-31 | **pass**: 50 MHz 4-bit (the top), bus 21.2 MB/s, end to end 9.1 MB/s with the echo device, 32 soak runs / 324 MB with 0 CRC errors; 230-233 mA busy |
| [test016 K230 DDR bandwidth](test016-ddr-bandwidth/README.md) | 2026-10-31 | **pass at 3200 MT/s all-ODT**: write 6.08 / read 4.84 / copy 2.77 GB/s, 8 x `ddr_bench 3072` |
| [test017 SPI NAND raw speed and SPI clock](test017-nand-raw-speed/README.md) | 2026-10-30 | **pass**: NAND at 50 MHz for stability, sample delay 5 (kernel + U-Boot/SPL); raw read 15.7 MB/s, 20/20 cold boots clean, ID read retried by the kernel. |
| [test018 K230 <-> BL616 UART link: speed, latency, errors](test018-uart-link-speed/README.md) | 2026-10-31 | **pass**: 2 Mbaud from boot; 8 x 1 MB, 0 errors, 180740 B/s (90 %), 16 B round trip 131 us; clean with the Wi-Fi bridge bound. |
| [test019 K230 USB-C 1: USB boot and flashing speed](test019-usb-flash-speed/README.md) | 2026-10-25 | **pass**: K230 whole image 24.6 s (erase 0-248 MB 8 s + 45.4 MB write, USB 2.0 high speed); |
| [test020 Camera interfaces: CSI frame rate, I2C clock](test020-camera-csi-i2c/README.md) | 2026-10-21 | **planned**: not run yet; waits for the cameras. |
| [test021 BL616: wake latency, flash storage, sensor reads](test021-bl616-internal-timing/README.md) | 2026-10-25 | **pass**: RTC wake -> main() 17.3-17.4 ms, K230_PWR -> 3V3 1.7 ms, AHT20 36.5 ms, leak ADC 2.6 ms |
| [test022 K230 boot time](test022-boot-time/README.md) | 2026-10-25 | **0.32 s**: reset release -> app init 0.320 / 0.320 / 0.321 s (min / median / max, 8 of 8 cold |
| [test023 Battery ADC (K230 ADC_1, VBAT/3)](test023-battery-adc/README.md) | 2026-10-25 | **pass**: calibrated 64-read mean within +-10 mV of the meter at 4.07-4.37 V; low cell and load open. |
| [test024 K230 heartbeat to the BL616 (K230_ALIVE)](test024-heartbeat/README.md) | 2026-10-25 | **pass**: 8 boots, half-period 500 / 500 / 500 ms (min / avg / max), no back-feed after |
| [test025 DDR stress (128 MB, 3200 MT/s all-ODT)](test025-ddr-stress/README.md) | 2026-10-31 | **pass at 3200 MT/s all-ODT**: 10 passes, 5 cold starts and memtest under KPU + encoder, 0 errors; 8 h soak 0 errors. |
| [test026 K230 die temperature (TS)](test026-temperature/README.md) | 2026-10-25 | **in progress**: open board idle and CPU/DDR (memtest) load pass, die 44.7 C idle, 52.7 C peak; |
| [test027 H.265 encoder with synthetic frames](test027-video-encoder/README.md) | 2026-10-25 | **pass**: 2 x 15 fps at the product settings (511 / 510 kbit/s); 1-channel top 75.9 fps (84.2 fps on |
| [test028 NAND / UFFS power-loss robustness](test028-nand-power-loss/README.md) | 2026-10-25 | **pass**: 520 random power cuts during writes over all runs (100 + 400 on 2026-10-23, 20 on |
| [test029 Camera buses and IO without cameras](test029-camera-bus-idle/README.md) | 2026-10-25 | **pass**: both camera I2C buses scan clean with no hang over 8 boots, idle high; CAM GPIOs low. |
| [test030 cameras: detection, capture, image](test030-camera-capture/README.md) | 2026-10-21 | **placeholder**: not run; the cameras are not delivered yet (two OV5647 with 222 degree M12 |
| [test031 sleep and minimal currents (every power state's floor)](test031-sleep-current/README.md) | 2026-10-25 | **pass, done**: on the battery the product sleep (HBN level 0, comparators armed) draws 35 uA, |
| [test032 RTC wake (BL616 HBN timer, 32.768 kHz crystal)](test032-rtc-wake/README.md) | 2026-10-25 | **partial**: HBN RTC wake on Y3 works (10 s and 600 s, every wake arrived); Y3 +17 ppm with a |
| [test033 every BL616 pin: function, levels, back-feed](test033-bl616-pins/README.md) | 2026-10-25 | **pass**: every pin as in the table in all 5 states (pt_pins + pin_check.py, 0 off); BOOT1 (IO27) |
| [test034 leak probe wake (ACOMP1) and wet/dry thresholds](test034-leak-probe-wake/README.md) | 2026-10-31 | **open: threshold decision**: tap, hot, salted water and puddles WET (243-756 mV vs 0.85 V); a pressed wet towel reads dry (949-952 mV); ~1.30 V proposed; shorts and HBN wake pass |
| [test035 USB plug wake (PGOOD on ACOMP0)](test035-usb-plug-wake/README.md) | 2026-10-22 | **pass**: plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported. |
| [test036 K230 hang recovery (heartbeat stop, READY timeout)](test036-k230-hang-recovery/README.md) | 2026-10-25 | **pass**: 12 supervised sessions (normal, hang, reboot, no-boot) all end with the rails off; a |
| [test037 K230 PMU RTC: start, set from the BL616, timekeeping](test037-k230-rtc/README.md) | 2026-10-25 | **partial: step 0 pass**: the PMU RTC runs from Y1, +27 ppm against the 27 MHz timer (3 boots), |
| [test038 battery brown-out below 3.10 V (cutoff, lockout, recovery)](test038-battery-brownout/README.md) | 2026-10-23 | **partial**: with a raised threshold on the cell the chain works - K230 `bat_watch` cut, |
| [test039 K230 console over the BL616 link (no CH340)](test039-k230-console-over-link/README.md) | 2026-10-31 | **pass**: 1000 of 1000 commands answered in each of 4 boots at 2 Mbaud, every drop counter 0; 226 mA answering back to back |
| [test040 BLE provisioning on the BL616: Wi-Fi credentials into flash](test040-ble-wifi-credentials/README.md) | 2026-10-29 | **pass**: run.py 46/46 (run 13, test firmware -O3 + LTO with the sdk_patch.py console fix) and |
| [test041 humidity alarm (AHT20 threshold wakes the K230)](test041-humidity-alarm/README.md) | 2026-10-30 | **planned**: the BL616 test firmware needs the humidity slice and alarm decision of |
| [test042 RF performance against the bench phone (RSSI side by side)](test042-rf-rssi-vs-phone/README.md) | 2026-10-31 | **position 1 done**: Wi-Fi receive board - phone -3..+1 dB, BLE -32..-43 dBm at the phone; one unexplained BL616 watchdog reset (recorder armed since); orientations, distance open |
| **High level (100+): the product's whole flow** | | |
| [test100 Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP](test100-wifi-throughput/README.md) | 2026-10-25 | **steps 1-4 pass**: K230 address 0.33-0.59 s after the card binds; TCP up 27.5 / down 22.1 Mbit/s, UDP up 35.4. |
| [test101 session length (wake to power-off)](test101-boot-and-session-time/README.md) | 2026-10-21 | **planned**: not run yet. |
| [test102 K230 session current](test102-session-current/README.md) | 2026-10-21 | **planned**: not run yet. |
| [test103 Whole leak pipeline, end to end](test103-leak-pipeline-timing/README.md) | 2026-10-21 | **later**: not run yet; excluded for now (user): needs the cameras, Wi-Fi and the product BL616 |
| [test104 battery energy audit](test104-battery-energy-audit/README.md) | 2026-10-30 | **planned**: when the BL616 test firmware runs the whole wake cycle for days (hibernate, sessions, Wi-Fi). |
| [test105 maximum current - can the battery carry everything at once?](test105-max-current/README.md) | 2026-10-25 | **partial**: handover runs 1-2 done - USB -> cell under every camera-free load (1.06 A at 5 V), |
| [test106 Wi-Fi setup from the phone, end to end](test106-wifi-setup-from-phone/README.md) | 2026-10-31, run.py 18/18 + hand_step7.py 5/5 | **pass, steps 1 3 4 5 6 7 8**: setup 7.3-8.0 s from opening the app to Done; on battery no board listed, no connection; step 2 open |
| [test107 change-net update from the server (GET /v1/nn)](test107-change-net-update/README.md) | 2026-10-30 | **planned**: K230 test image with `leakcam_wake`, sessions and the Wi-Fi bridge from the BL616 test |
| [test108 production server check by the BL616 (schedule, lease reuse, server address, back-off)](test108-server-check-schedule/README.md) | 2026-10-30 | **planned**: the scheduler, lease reuse and server address are not written yet (DESIGN 5.1 |
| [test109 secure server (TLS, device identity, pinned certificate, signed change net)](test109-secure-server/README.md) | 2026-10-30 | **planned**: the real server is not built (DESIGN 7, SECURITY.txt 4); it will run on the Windows PC for the test. |
| [test110 setup app recovery from loss at every step](test110-ble-loss-recovery/README.md) | 2026-11-01, run.py 37/37 | **pass**: lost notifications, a link lost after the save, Bluetooth off during the check: the app reconnects and shows the board's result; failures named in 5.7-11.1 s; app and board agree in every case |
| [test111 more than one LEAKCAM: one device ID, presence through the server](test111-multi-device/README.md) | 2026-11-01, run.py 12/12 | **pass**: one ID on the board, USB and BLE; online at the server 39 s after a setup; the app shows each LEAKCAM online / offline |

## Not tested yet (audit 2026-10-30)

**Product behaviour no test covers** (DESIGN.md 5; most need the code added to the test firmware, or the cameras):

| Behaviour | Where it belongs |
|---|---|
| What the BL616 does after each wake (the wake sources themselves pass on the test firmware: RTC test032, leak probe test034, USB plug test035, timing test021): doing what is due (humidity, report, server check with / without a stream request) and overlaps (a leak during a check, a check with a report due, a check while on USB; no AP, no server, no credentials) | new test, on the test firmware with the wake handling from firmware/bl616 (DESIGN 5.1 asks for it) |
| Humidity alarm (AHT20 threshold -> report) | test041 (planned) |
| `leakcam_wake` on the board: capture -> history compare (imgdiff + change net) -> `/v1/check` to the mock server -> the sleep it chooses | test103 |
| Change-net update from the server (`GET /v1/nn`, bad downloads, power cut while saving, unloadable net) | test107 (planned) |
| Session protocol end to end (READY / WAKE / ACK / TIME / SLEEP) with the product agent; NTP time back to the BL616 | test101, test037 steps 1-6 |
| Live stream: both cameras + Opus audio over RTMP to MediaMTX (`leakcam_stream -p`) | test100 step 5 (cameras) |
| OTA: the writer (inactive slot + `ota_meta`) is not built; the SPL's A/B choice never ran on the board | new test, when the writer exists |
| Production server check (time schedule, DHCP lease reuse, server interval, `SERVER` from the agent, back-off) | test108 (planned; the code is not built, test100 `pt_poll` proved the cycle) |
| Cameras: CSI/I2C, capture at 2592x1944, LED brightness at 2-3 m, auto-exposure against the LED back-off | test020, test030, test009 |
| The closed enclosure: temperature, Wi-Fi at the install distance | test026, test100 |
| The real server (TLS, device identity, pinned certificate, signed change net, RTMPS) | test109 (planned; the server is not built, it runs on the Windows PC) |

**Tests not run or partial:** not run - test020, test030 (cameras), test101, test102, test104 (test-firmware features to add),
test103 (later); partial - test008 (steps 3-7), test026 (KPU / encoder / camera load, enclosure, die limit), test032
(crystal ppm, 100 cycles, 24 h), test034 (threshold decision, 8 h dry), test035 (100 transitions), test037 (steps 1-6),
test038 (on the second board), test105 (runs 1-4), test106 (step 2); **fail** - none.

**Always:**
- Build host = VMware VM on a Mac. Since 2026-10-24 VMware's USB popups are off: every re-enumerated device (K230
  boot ROM and loader, BL616 app and ROM) connects to Linux by itself, so flashes of both chips, K230 power cycles and
  soak / leak loops run **unattended** (background runners that log and recover by themselves). Only physical events
  need hands: USB-C 1 plug / unplug, probe wetting, meter readings. A device missing from `lsusb` is a board or
  firmware fault now, not a missed click. (Before: "connect to Linux" per re-enumeration; the results below say so.)
- Serial ports: /etc/udev/rules.d/99-leakcam.rules opens them on every plug. Consoles: `/dev/ttyACM0` BL616 shell,
  `/dev/ttyACM1` K230 console (over the link UART since 2026-10-22, from the first SPL line; the CH340X `/dev/ttyUSB0`
  only for the BootROM).
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
- Audio played inside the VM stutters: judge recordings by their data or play them on the Mac.
