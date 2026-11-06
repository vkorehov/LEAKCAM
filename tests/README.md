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

**Standard:** `tools/test-runner/check_tests.py` checks every README's sections and the 14 metric rows of skill
leakcam-test-metrics (a value measured, `n/a: <reason>`, or `**MISSING**`); after a run `check_tests.py testNNN` must
pass before the run is reported. Every test records boot time, battery energy, temperature and memory on both chips;
every repeated operation is a leak test (`tools/power-bringup/mem_leak.py`). Tests run on the **test firmware only**
(the product firmware is rewritten after the tests), built with the SDK's default flags (-Os -g, no LTO) for fast builds; -O3 / LTO
only in tests about optimisation (user, 2026-10-29).

**Automated runs:** a test with a `run.py` runs unattended through PR1; `tools/test-runner/run_tests.py` runs them in
turn (output in the test's `run.log`). `tools/test-runner/harness.py` times the K230 cold boot, samples temperature,
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
| [test034 leak probe wake (ACOMP1) and wet/dry thresholds](test034-leak-probe-wake/README.md) | 2026-10-31 | **threshold changed to 1.10 V, re-check pending**: water, puddles, hot and salted water WET; a pressed wet towel reads dry |
| [test035 USB plug wake (PGOOD on ACOMP0)](test035-usb-plug-wake/README.md) | 2026-10-22 | **pass**: plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported. |
| [test036 K230 hang recovery (heartbeat stop, READY timeout)](test036-k230-hang-recovery/README.md) | 2026-10-25 | **pass**: 12 supervised sessions (normal, hang, reboot, no-boot) all end with the rails off; a |
| [test037 K230 PMU RTC: start, set from the BL616, timekeeping](test037-k230-rtc/README.md) | 2026-10-25 | **partial: step 0 pass**: the PMU RTC runs from Y1, +27 ppm against the 27 MHz timer (3 boots), |
| [test038 battery brown-out below 3.10 V (cutoff, lockout, recovery)](test038-battery-brownout/README.md) | 2026-10-23 | **partial**: with a raised threshold on the cell the chain works - K230 `bat_watch` cut, |
| [test039 K230 console over the BL616 link (no CH340)](test039-k230-console-over-link/README.md) | 2026-10-31 | **pass**: 1000 of 1000 commands answered in each of 4 boots at 2 Mbaud, every drop counter 0; 226 mA answering back to back |
| [test040 BLE provisioning on the BL616: Wi-Fi credentials into flash](test040-ble-wifi-credentials/README.md) | 2026-11-03 | **pass**: run.py 37/37 on the setup firmware's test bed, the phone 8 m away: PIN gate, write limits, persistence, checked store to the API, 8 store cycles without a leak; step 8 gone with the firmware split |
| [test041 humidity alarm (AHT20 threshold wakes the K230)](test041-humidity-alarm/README.md) | 2026-10-30 | **planned**: the BL616 test firmware needs the humidity slice and alarm decision of |
| [test042 RF performance against the bench phone (RSSI side by side)](test042-rf-rssi-vs-phone/README.md) | 2026-10-31 | **position 1 done**: Wi-Fi receive board - phone -3..+1 dB, BLE -32..-43 dBm at the phone; one unexplained BL616 watchdog reset (recorder armed since); orientations, distance open |
| [test043 the K230's USB device on USB-C 1](test043-k230-usb-device/README.md) | 2026-11-05 | **pass**: CherryUSB CDC ACM (1209:ABD1) up 0.73 s after the K230 start, seen by the VM at 4.1-4.4 s (~3.3 s VMware passthrough); 200/200 16-byte round trips; 1 MB echoed, 0 wrong, 3317 KB/s (test image only) |
| [test044 the phone sees the board over a USB-C cable](test044-phone-usb-link/README.md) | 2026-11-05 | **steps 1-6 pass**: phone as USB host lists the K230 device 1.41 s after its start (median of 5, max 1.58 s), gone 0.28 s after power-off; step 7 (app icon) waits for the app |
| [test045 link frames reach a K230 program through /dev/link](test045-k230-link-frames/README.md) | 2026-11-05 | **pass**: 5/5 starts the K230 program gets ACK + WAKE (READY 0.43 s after pt_run, READY -> WAKE 8 ms) after the fix: link_mux set `/dev/link`'s file ops before `rt_device_register()`, which clears them; devfs's poll never reported data, no K230 program had ever got a frame |
| [test046 the K230's battery reading on every link frame, kept by the BL616](test046-battery-attribute/README.md) | 2026-11-06 | **partly run**: 1 (USB), 6, 7 pass; battery steps 2-5 by hand open |
| [test047 the K230 from the USB relay to the normal product job](test047-k230-relay-to-session/README.md) | 2026-11-06 | **partly run**: 1, 2, 3, 6, 7 pass; 4 by hand, 5 not built |
| [test048 every metric in every check-in](test048-checkin-metrics/README.md) | 2026-11-06 | **pass, automated part** (33/33); hand steps 2-4, 6, 7, 12 open; AHT20 reads the warm board on USB |
| **High level (100+): the product's whole flow** | | |
| [test100 Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP](test100-wifi-throughput/README.md) | 2026-10-25 | **steps 1-4 pass**: K230 address 0.33-0.59 s after the card binds; TCP up 27.5 / down 22.1 Mbit/s, UDP up 35.4. |
| [test101 session length (wake to power-off)](test101-boot-and-session-time/README.md) | 2026-10-30 | **pass, steps 1-2**: 10/10 sessions, power-on -> READY 0.87 s, rails off 27.68 s |
| [test102 K230 session current](test102-session-current/README.md) | 2026-10-21 | **planned**: not run yet. |
| [test103 Whole leak pipeline, end to end](test103-leak-pipeline-timing/README.md) | 2026-10-21 | **later**: not run yet; excluded for now (user): needs the cameras, Wi-Fi and the product BL616 |
| [test104 battery energy audit](test104-battery-energy-audit/README.md) | 2026-10-30 | **planned**: when the BL616 test firmware runs the whole wake cycle for days (hibernate, sessions, Wi-Fi). |
| [test105 maximum current - can the battery carry everything at once?](test105-max-current/README.md) | 2026-10-30 | **pass on USB**: per-load currents recorded, all camera-free loads 10 min with 0 K230 resets |
| [test106 Wi-Fi setup from the phone, end to end](test106-wifi-setup-from-phone/README.md) | 2026-10-31, run.py 18/18 + hand_step7.py 5/5 | **pass, steps 1 3 4 5 6 7 8**: setup 7.3-8.0 s from opening the app to Done; on battery no board listed, no connection; step 2 open |
| [test107 change-net update from the server (GET /v1/nn)](test107-change-net-update/README.md) | 2026-10-30 | **planned**: K230 test image with `leakcam_wake`, sessions and the Wi-Fi bridge from the BL616 test |
| [test108 production server check by the BL616 (schedule, lease reuse, server address, back-off)](test108-server-check-schedule/README.md) | 2026-10-30 | **planned**: the scheduler, lease reuse and server address are not written yet (DESIGN 5.1 |
| [test109 secure server (TLS, device identity, pinned certificate, signed change net)](test109-secure-server/README.md) | 2026-10-30 | **planned**: the real server is not built (DESIGN 7, SECURITY.txt 4); it will run on the Windows PC for the test. |
| [test110 setup app recovery from loss at every step](test110-ble-loss-recovery/README.md) | 2026-11-03, run.py 30/30 | **pass** on the setup firmware's test bed (pt_fault), the phone 8 m away: lost notifications, a link lost after the save, Bluetooth off during the check -> Done 9.4-15.2 s; failures named in 6.4-8.9 s; app and board agree |
| [test111 more than one LEAKCAM: one device ID, presence, status screen](test111-multi-device/README.md) | 2026-11-01, run.py 18/18 | **pass**: one ID on board, USB and BLE; the app's status screen (online / offline / delete), the short change-Wi-Fi flow, no stored network |
| [test113 full setup from the phone app over a USB-C cable (product firmware)](test113-usb-setup-from-phone/README.md) | 2026-11-06 | **pass**: the phone's plug opens the app on the cable screen (4.3 s), setup over the cable to Done, network saved, full firmware online; 65 s run |
| [test114 the lowest BLE energy at which the phone finds the board](test114-ble-discovery-energy/README.md) | 2026-11-01 | **part A run**: found every time up to 2 s advertising (median 0.2-2.2 s), misses from 5 s; currents and parts B, C open |
| [test116 the whole setup over BLE alone (no USB)](test116-ble-only-setup/README.md) | 2026-11-01 | **planned**: on battery, USB-C 1 out (hand): found in an advertising window, set up from the app |
| [test117 presence lost brings BLE back](test117-presence-loss-ble/README.md) | 2026-11-03 | **pass** 19/19 on the product pair, the phone 8 m away: server down / network gone -> the setup firmware in 9 / 13 s, found at once (RSSI -70), set up in 8.7-9.1 s, back in the full firmware 4 s after |
| [test118 the app's icons follow the board](test118-app-presence-icons/README.md) | 2026-11-01, 7/7 | **pass**: BLE icon goes 51 s after advertising stops (45 s unseen + scan), Wi-Fi icon and red 29 s after presence stops, both back in 11-20 s |
| [test119 the phone wakes a board through the server](test119-wake-through-server/README.md) | 2026-11-03 | **pass** 16/16 on the product pair, the phone 8 m away: Wake tap -> the setup firmware at the next check-in (11 s) -> Wi-Fi changed 8.8 s -> full 4 s after; second wake 429; an unanswered wake back to full after 120 s |
| [test120 the BLE low-power setup mode alone](test120-ble-low-power-setup-mode/README.md) | 2026-11-01 | **in progress**: product BLE asleep in PDS at 2 s advertising, phone finds it 10/10 (median 1.1 s); the BLE core wedge found and fixed; connection wake next |
| [test121 USB plug wakes the board from BLE sleep](test121-usb-plug-wake-ble-sleep/README.md) | 2026-11-01 | **pass** 2/2: PGOOD (IO3) wakes PDS, no sleep while USB is in, sleeps again after unplug |
| [test122 battery low: the K230 stops, the BL616 tells the user to charge](test122-low-battery-notify/README.md) | 2026-11-01 | **planned**: quick K230 stop below 3.10 V, one Wi-Fi check-in with the battery-low flag, the BLE flag, lockout until USB |
| [test123 change the Wi-Fi network remotely, fallback in 15 s](test123-remote-wifi-change/README.md) | 2026-11-01 | **planned**: new network from the app through the server, checked in 15 s, else back on the previous one with the reason |
| [test124 the BL616's own brown-out protection](test124-bl616-brownout/README.md) | 2026-11-01 | **planned**: BOD config read back (on, 2.40 V, reset), the reset record per reset kind; trip, slow collapse and a write in flight with a lab supply (next board) |
| [test125 setup mode retries presence every 6 h](test125-presence-retry/README.md) | 2026-11-01 | **planned**: one check-in attempt every 6 h on the stored network; success -> back to the full firmware; the whole operational -> setup -> operational cycle |
| [test126 the slot switch between the full and the setup firmware](test126-firmware-slot-switch/README.md) | 2026-11-02 | **pass** steps 1-7: 20 switches both ways, 4.3 s each, settings shared, re-flash after switches boots the flashed image; the crash record now names its image |
| [test127 the setup firmware hands over on a USB plug, checked to the API](test127-usb-handover-to-full/README.md) | 2026-11-02 | **pass** 1-6: a real plug from BLE sleep hands over; server down 0x19, wrong password 0x11, no network: setup stays; BLE setup hands over after SAVED |
| [test128 crash records reach the server over Wi-Fi](test128-crash-record-to-server/README.md) | 2026-11-02 | **pass** 1 2 3 5 7: exception and hang at the server 4.1 s after the reset, kept while the server is down, sent by the setup firmware too, count 2 for two; records on disk at the server |
| [test129 app power and signal icons](test129-app-power-signal-icons/README.md) | 2026-11-04 | **pass** 6/6: plug icon on USB power, battery icon on battery, signal bars match the RSSI, long press shows the dB |
| [test130 OTA upgrade of the BL616 full firmware and the K230 image](test130-ota-upgrade/README.md) | 2026-11-06 | **BL616 part pass** (1-4, 6, 13; 27 s offer to running); step 5 by hand, K230 steps not built |
| [test131 the app's battery icon from every channel](test131-app-battery-level/README.md) | 2026-11-06 | **planned**: built (app, server, BL616 pair, K230 image), not run |

## Re-test audit after the two-firmware design (2026-11-02)

The design changes behind it (DESIGN.md 5.3, 5.3.1, 5.3.3):
- Two BL616 firmwares in two slots. The full firmware has no BLE. Every setup activity (BLE, the USB cable) runs in
  the setup firmware.
- The network check ends at the API (a check-in answered by the server). New failure: 0x19, API not reachable.
- A USB plug hands over to the full firmware only after that check passes.
- Crash records carry their build and go to the server over Wi-Fi.
- One version for both images, flashed as a pair.

**Run on battery too (`run.py --battery`, USB-C 1 out; added 2026-11-03 after bugs/bug-006):** every setup-firmware
pass so far was on USB, where it never sleeps; on battery it sleeps, its console is off and the phone's scan stands
for it. Hands: USB-C 1 out at the start, in at the end.

| Test | On battery |
|---|---|
| test106 | 8 setups in a row from the full firmware: found asleep, Done, back in the full firmware |
| test116 | already on battery: pass 16/16 (2026-11-03) |
| test117 | server down and network gone: the setup firmware found asleep, set up, present again |
| test119 | wake, change and the unanswered wake's 2 minutes, asleep |
| test125 | the 6 h retry is a battery path by nature: plan it on battery from the start |

**Re-prove: the pass is on the old design (the full firmware's own BLE, the check ending at the internet):**

| Test | What changes | Blocked by |
|---|---|---|
| test040 | Re-proved 2026-11-03 on the setup test bed; left: 0x19 (API not reachable) in step 7, a server-down case. | - |
| test106 | Runs on the setup firmware; its "Setup mode" section becomes the main path. Step 2 (on battery) is the full firmware after the hand-over. Step 3 adds server down -> "API not reachable" in the app. | as test040 |
| test110 | Re-proved 2026-11-03 on the setup test bed; left: the new case "the API answer lost". | - |
| test111 | Step 1 reads the ID from the setup firmware's advertisement. Step 5 "set up again on USB power" now goes through the server: the full firmware switches to setup. | full -> setup trigger |
| test116 | The setup firmware advertises on battery all the time (no 5-min windows). Step 4 "silent" means the full firmware. Step 5 (USB back in, network stored) means the hand-over, not advertising. | full -> setup trigger |
| test042 | Steps 3-4 (BLE transmit, 3 m) move to the setup firmware (2 s advertising in PDS); the Wi-Fi steps stay. | - |

**Rework the plan before the first run (not run yet):**

| Test | What changes |
|---|---|
| test113 | Done: the cable setup runs in the setup firmware (K230 session WAKE setup, USB line protocol), pass 2026-11-06. |
| test114 | Part A holds. Part B (wake beacon) is replaced by the server wake (test119). Part C is test120. The choice (2 s) lives in the setup firmware, not provision.c. Close it. |
| test120 | Step 7 (USB wake) is done by test121 + test127. Step 5 (full setup from low power) is the test040/106 re-proof. Steps 4 and 6, the battery-side current and the crash-recorder start deadline are still open. |
| test121 | Step 5: with USB in and no network, the setup firmware stays awake (it no longer hands over regardless); the hand-over is test127. README text only; the result holds. |
| test122 | The BLE flag goes in the setup firmware's advertisement. The USB notice (3a) goes through the setup firmware's cable path. The lockout and the flag must survive a slot switch (HBN RAM at a fixed address, like the crash record). Step 6 becomes the switch to setup with the flag. |
| test123 | The failure list adds "API not reachable" (0x19). Otherwise unchanged: full firmware, over the server. |
| test124 | Steps 1-2 done today (record per reset kind). Step 1 needs a read in the full firmware too. Step 5 adds a partition-table write in flight (a slot switch during the drop): the board must boot one slot or the other. |
| test125 | Step 5: a USB plug with presence still lost keeps the setup firmware (test127: the check fails at the API), not "the full firmware takes over". |
| test108 | Adds an at-once check-in at boot when a crash record is kept, 60 s / 24 h intervals, and a failed check-in -> setup. |
| test104 | Adds the setup mode's day (PDS + 2 s advertising) and the 6 h retries. |
| test008 | The image feature set changes: the full firmware loses BLE. Re-size both images. |

**Still valid as passed:**
- Hardware and K230 tests: 001, 003-039, 100, 101, 105 and 115. They don't run the changed code paths, or they run
  them only on the test firmware.
- test118: the app's icon logic. The bench stands in for the board.
- test126, test127 and test128: new and passed today. Open: test128 steps 4 and 6, test127's setup bed hang record.
- test002 and test019 hold for the test firmware. The product pair flash (`make flash` writes both partition tables
  and slot 1) still needs a timing in test002.

**Code needed before those re-runs:**
- The full -> setup triggers: no network, failed check-in, "ble":1. Then the full firmware's BLE comes out
  (provision.c, ble_pairing.c).
- `pt_ble` / `pt_fault` equivalents in bl616_setup_test.
- The cable relay (test113), the 6 h retry (test125), the battery-low flag across slots (test122), the remote change
  (test123) and the check-in intervals.

## Not tested yet (audit 2026-10-30)

**Product behaviour no test covers** (DESIGN.md 5; most need the code added to the test firmware, or the cameras):

| Behaviour | Where it belongs |
|---|---|
| What the BL616 does after each wake (the wake sources themselves pass on the test firmware: RTC test032, leak probe test034, USB plug test035, timing test021): doing what is due (humidity, report, server check with / without a stream request) and overlaps (a leak during a check, a check with a report due, a check while on USB; no AP, no server, no credentials) | new test, on the test firmware with the wake handling from attic/firmware/bl616 (DESIGN 5.1 asks for it) |
| Humidity alarm (AHT20 threshold -> report) | test041 (planned) |
| `leakcam_wake` on the board: capture -> history compare (imgdiff + change net) -> `/v1/check` to the mock server -> the sleep it chooses | test103 |
| Change-net update from the server (`GET /v1/nn`, bad downloads, power cut while saving, unloadable net) | test107 (planned) |
| Session protocol end to end (READY / WAKE / ACK / TIME / SLEEP) with the product agent; NTP time back to the BL616 | test101, test037 steps 1-6 |
| Live stream: both cameras + Opus audio over RTMP to MediaMTX (`leakcam_stream -p`) | test100 step 5 (cameras) |
| OTA: the writer (inactive slot + `ota_meta`) is not built; the SPL's A/B choice never ran on the board | test130 (planned), with the BL616 full firmware too |
| Production server check (time schedule, DHCP lease reuse, server interval, `SERVER` from the agent, back-off) | test108 (planned; the code is not built, test100 `pt_poll` proved the cycle) |
| Cameras: CSI/I2C, capture at 2592x1944, LED brightness at 2-3 m, auto-exposure against the LED back-off | test020, test030, test009 |
| The closed enclosure: temperature, Wi-Fi at the install distance | test026, test100 |
| The real server (TLS, device identity, pinned certificate, signed change net, RTMPS) | test109 (planned; the server is not built, it runs on the Windows PC) |

**Tests not run or partial:** not run - test020, test030 (cameras), test102, test104 (test-firmware features to add),
test103 (later); partial - test008 (steps 3-7), test026 (KPU / encoder / camera load, enclosure, die limit), test032
(crystal ppm, 100 cycles, 24 h), test034 (re-check at 1.10 V, 8 h dry), test035 (100 transitions), test037 (steps 1-6),
test038 (on the second board), test106 (step 2); **fail** - none.

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
