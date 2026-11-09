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

## Essential suite (run after every change)

`tools/test-runner/run_tests.py --essential` (user, 2026-11-08: "a test suite to run always, essential tests, fast,
no stability testing, skip the basic hardware tests"). It runs the server's Go tests, then these, one pass each, in
this order. Needs: the product pair flashed (firmware/bl616_full `make flash`), the board on the full firmware with
its network, USB-C 1 through the bench relay box (skill leakcam-bench-relays: it unplugs, plugs and moves USB-C 1 to
the phone, so the former hand tests run too, user 2026-11-09), the phone on adb; no hands, no meter. Before the batch
and after every test: no BLE core wedge (`--wedge-probe`: a sleep probe after each test too, test139 A1000).

| Test | Why it is essential |
|---|---|
| test135 app screens | every screen and flow of the app (fake board): LEAKCAMs, Setup, Change Wi-Fi, pending, leak, links |
| test133 leak alert | the most important use case: the alert's look, picture, "I see it" |
| test134 Wi-Fi change via server | the only way to change a set-up board's network |
| test108 server check schedule | the check-in and leak-check intervals the server sets |
| test128 crash record to server | a crash on either firmware reaches the server |
| test126 firmware slot switch | the full <-> setup switch every flow rides on (2 round trips) |
| test138 clock without NTP | the wall clock from the server's answers (BL616 and K230) |
| test106 setup over Bluetooth | the setup a person does: Done, wrong password, network not seen, save without checking |
| test111 several boards, delete | the phone's list from the server, delete -> setup firmware |
| test117 presence lost | a board gone from Wi-Fi: delete, back in Setup |
| test129 setup list icons | power, signal bars, dB on a long press |
| test106 --battery | the setup on the cell (the setup firmware asleep, 2 s advertising) |
| test117 --battery | presence lost on the cell: the board found asleep, set up again |
| test129 --battery | the battery icon after the unplug |
| test116 BLE-only setup | the whole setup with no USB: asleep, set up, erased on the cell, the switch back (bug-006's path) |
| test113 cable setup from the phone | the USB cable setup: USB-C 1 moved to the phone and back |

Not in it: repeats and soaks (`--stress`: test106's 8 setups, test126's 10 round trips); tests that still need hands or
the meter (test102 meter, test132: the leak probe wet/dry has no relay yet); test139 replug stability (~11 min); test110
(losses during a setup: recovery cases, ~4 min), test124 (reset records) and test101 (boot and job timing), rewritten
on the product pair 2026-11-08: measurements and recovery, run before a release or when their code changes; energy runs (test114); the 001-099 bring-up tests (one chip or link
each, proven on rev 1; rerun one only when its hardware or driver changes). Hand tests left (test132's probe) are run together, grouped, when the user is at the bench.

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
| [test006 K230 NAND boot (SPL)](test006-k230-nand-boot-spl/README.md) | 2026-10-25 | **pass**: 5 of 5 cold boots from NAND (SPL, DDR training, OpenSBI, kernel, msh); DDR 3200 MT/s all-ODT since 2026-10-23 (test025); reset |
| [test007 RT-Smart boot](test007-rtsmart-boot/README.md) | 2026-10-25 | **pass**: 5 of 5 cold boots from NAND to msh, /bin and /sdcard mounted, agent starts; PMU RTC |
| [test008 minimal image - the fewest bits that still boot and pass](test008-minimal-image/README.md) | 2026-10-23 | **in progress**: builds 1+2 adopted (app init 0.484-0.494 s); kernel stays -O1; programs |
| [test009 LED chains (white U14/J1, IR U15/J2)](test009-led-chains/README.md) | 2026-10-25 | **pass**: both chains light and reach full current at 100 % duty on the K230 PWM (white +0.34 A, |
| [test010 K230 <-> BL616 UART link](test010-uart-link/README.md) | 2026-10-31, 2 Mbaud | **pass**: 8 boots, 112 READY frames K230 -> BL616 with valid checksums, BL616 -> K230 |
| [test011 NAND persistence, full disk, speed](test011-nand-fill-speed/README.md) | 2026-10-25 | **pass**: files survive a power cycle, a full disk fails cleanly, read 14.4 MB/s, write 2.1 MB/s. |
| [test012 microphone U13 (MSM381ACB026) on the K230 inner codec](test012-microphone/README.md) | 2026-10-25 | **pass**: 3 x 10 s recordings, a level every second, rms -25.3..-2.8 dBFS; no dropouts, no |
| [test013 KPU, change net (leakcam_change.kmodel)](test013-kpu-change-net/README.md) | 2026-10-25 | **pass**: int8 net whole on the KPU, 0.5 ms per inference, one distance 2.9 ms median, output |
| [test014 SDIO wiring, K230 MMC0 <-> BL616 SDIO](test014-sdio-wiring/README.md) | 2026-10-25 | **pass**: CLK, CMD, D0-D3 connected both directions, none stuck, no shorts; CLK and CMD toggle |
| [test015 SDIO speed, K230 MMC0 <-> BL616 SDIO device](test015-sdio-speed/README.md) | 2026-10-31, run.py | **pass**: 50 MHz 4-bit (the top), bus 21.2 MB/s, end to end 9.1 MB/s with the echo |
| [test016 K230 DDR bandwidth](test016-ddr-bandwidth/README.md) | 2026-10-25 | **pass at 3200 MT/s all-ODT**: write 6.08 / read 4.84 / copy 2.77 GB/s, 8 x `ddr_bench 3072` |
| [test017 SPI NAND raw speed and SPI clock](test017-nand-raw-speed/README.md) | 2026-10-30 | **pass**: NAND at 50 MHz, sample delay 5 (kernel + U-Boot/SPL), for stability; raw read 15.7 MB/s, |
| [test018 K230 <-> BL616 UART link: speed, latency, errors](test018-uart-link-speed/README.md) | 2026-10-31 | **pass**: the link runs 2 Mbaud from boot (stability, test039); 8 x 1 MB echoed with 0 errors |
| [test019 K230 USB-C 1: USB boot and flashing speed](test019-usb-flash-speed/README.md) | 2026-10-25 | **pass**: K230 whole image 24.6 s (erase 0-248 MB 8 s + 45.4 MB write, USB 2.0 high speed); |
| [test020 Camera interfaces: CSI frame rate, I2C clock](test020-camera-csi-i2c/README.md) | 2026-10-21 | **planned**: not run yet; waits for the cameras. |
| [test021 BL616: wake latency, flash storage, sensor reads](test021-bl616-internal-timing/README.md) | 2026-10-25 | **pass**: RTC wake -> main() 17.3-17.4 ms, K230_PWR -> 3V3 1.7 ms, AHT20 36.5 ms, leak ADC 2.6 ms |
| [test022 K230 boot time](test022-boot-time/README.md) | 2026-10-25 | **0.32 s**: reset release -> app init 0.320 / 0.320 / 0.321 s (min / median / max, 8 of 8 cold |
| [test023 Battery ADC (K230 ADC_1, VBAT/3)](test023-battery-adc/README.md) | 2026-10-25 | **pass**: calibrated 64-read mean within +-10 mV of the meter at 4.07-4.37 V; low cell and load open. |
| [test024 K230 heartbeat to the BL616 (K230_ALIVE)](test024-heartbeat/README.md) | 2026-10-25 | **pass**: 8 boots, half-period 500 / 500 / 500 ms (min / avg / max), no back-feed after |
| [test025 DDR stress (128 MB, 3200 MT/s all-ODT)](test025-ddr-stress/README.md) |  | **pass at 3200 MT/s all-ODT** (2026-10-31: 10 passes, 5 cold starts and the run under KPU + encoder, 0 |
| [test026 K230 die temperature (TS)](test026-temperature/README.md) | 2026-11-01, run.py 1364 s | **in progress**: open board, every load pass: die 37.8 C idle, memtest peak 53.6 C, |
| [test027 H.265 encoder with synthetic frames](test027-video-encoder/README.md) | 2026-10-25 | **pass**: 2 x 15 fps at the product settings (511 / 510 kbit/s); 1-channel top 75.9 fps (84.2 fps on |
| [test028 NAND / UFFS power-loss robustness](test028-nand-power-loss/README.md) | 2026-10-25 | **pass**: 520 random power cuts during writes over all runs (100 + 400 on 2026-10-23, 20 on |
| [test029 Camera buses and IO without cameras](test029-camera-bus-idle/README.md) | 2026-10-25 | **pass**: both camera I2C buses scan clean with no hang over 8 boots, idle high; CAM GPIOs low. |
| [test030 cameras: detection, capture, image](test030-camera-capture/README.md) | 2026-10-21 | **placeholder**: not run; the cameras are not delivered yet (two OV5647 with 222 degree M12 |
| [test031 sleep and minimal currents (every power state's floor)](test031-sleep-current/README.md) | 2026-10-25 | **pass, done**: on the battery the product sleep (HBN level 0, comparators armed) draws 35 uA, |
| [test032 RTC wake (BL616 HBN timer, 32.768 kHz crystal)](test032-rtc-wake/README.md) | 2026-11-01, run.py 1285 s, again | **partial**: HBN RTC wake on Y3 works (10 s -> 13.6-14.6 s, 600 s -> |
| [test033 every BL616 pin: function, levels, back-feed](test033-bl616-pins/README.md) | 2026-10-25 | **pass**: every pin as in the table in all 5 states (pt_pins + pin_check.py, 0 off); BOOT1 (IO27) |
| [test034 leak probe wake (ACOMP1) and wet/dry thresholds](test034-leak-probe-wake/README.md) | 2026-10-31 | **threshold changed to 1.10 V, re-check pending**: water in a cup, a puddle, hot and salted water |
| [test035 USB plug wake (PGOOD on ACOMP0)](test035-usb-plug-wake/README.md) | 2026-10-22 | **pass**: plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported. |
| [test036 K230 hang recovery (heartbeat stop, READY timeout)](test036-k230-hang-recovery/README.md) | 2026-10-25 | **pass**: 12 supervised sessions (normal, hang, reboot, no-boot) all end with the rails off; a |
| [test037 K230 PMU RTC: start, set from the BL616, timekeeping](test037-k230-rtc/README.md) | 2026-11-01, again: +28, +28, +27 ppm, 3 boots | **partial: step 0 pass**: the PMU RTC runs from Y1, about |
| [test038 battery brown-out below 3.10 V (cutoff, lockout, recovery)](test038-battery-brownout/README.md) | 2026-10-23 | **partial**: with a raised threshold on the cell the chain works - K230 `bat_watch` cut, |
| [test039 K230 console over the BL616 link (no CH340)](test039-k230-console-over-link/README.md) | 2026-10-30 | **pass**: 1000 of 1000 commands answered in each of 4 boots at 2 Mbaud, every drop counter 0; |
| [test040 BLE provisioning on the BL616: Wi-Fi credentials into flash](test040-ble-wifi-credentials/README.md) | 2026-11-03 | **pass**: run.py 37/37 in 368 s on the setup firmware's test bed (firmware/bl616_setup's own |
| [test042 RF performance against the bench phone (RSSI side by side)](test042-rf-rssi-vs-phone/README.md) | 2026-10-31, 10 runs, board flat, phone flat beside it | **position 1 done**: Wi-Fi receive board - phone |
| [test043 the K230's USB device on USB-C 1 (low level, before USB setup)](test043-k230-usb-device/README.md) | 2026-11-05 | **pass**: the K230 enumerates as CDC ACM on USB-C 1 and echoes 1 MB without a wrong byte. |
| [test044 the phone sees the board over a USB-C cable (low level: the USB link the app will use)](test044-phone-usb-link/README.md) | 2026-11-05 | **steps 1-6 pass**: the phone (USB host, powering the cable) lists the board's USB device 1.41 s |
| [test045 link frames reach a K230 program through /dev/link (low level: the BL616 <-> K230 link, both ways)](test045-k230-link-frames/README.md) | 2026-11-05, after the kernel fix below | **pass**: 5/5 K230 starts, the program got ACK + WAKE setup every |
| [test046 the K230's battery reading on every link frame, kept by the BL616 (low level: the link, both firmwares)](test046-battery-attribute/README.md) | 2026-11-06, from test136's relay: test136-checkin-metrics/relay_*.log | **partly run**: step 1 on USB power |
| [test047 the one K230 program on every power-up: the relay always, the leak job beside it (low level)](test047-k230-relay-to-session/README.md) | 2026-11-07, in other tests' runs; no run.py of its own yet | **partly seen**: step 1 in both firmwares (full: |
| [test051 maximum current - can the battery carry everything at once?](test051-max-current/README.md) | 2026-10-30 | **pass on USB**: per-load currents recorded; all camera-free loads 10 min with 0 K230 resets, die |
| **High level (100+): the product's whole flow** | | |
| [test100 Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP](test100-wifi-throughput/README.md) | 2026-10-25 | **steps 1-4 pass**: K230 address 0.33-0.59 s after the card binds; TCP up 27.5 / down 22.1 Mbit/s, UDP up 35.4. |
| [test101 session length (wake to power-off)](test101-boot-and-session-time/README.md) | 2026-11-08, run.py on the product pair, 40 s | **pass**: K230 cold session (`k230 off`, the full firmware's restart on USB power) 3V3 in 1.7 ms, first heartbeat 834 ms, READY 836 ms after the run; a leak check on USB (CHECK to the running K230 -> DONE) 109-110 ms. The 2026-10-30 numbers (test firmware's `pt_session`) are in HISTORY.md. |
| [test102 K230 session current](test102-session-current/README.md) | 2026-11-08, user: "defer to later" | **deferred**: to be rewritten on the product pair (the energy of one leak check; test132 has its timing: the K230 on ~1.6 s) and read with the cell out or full; its run.py is the old test firmware's (`pt_session`); a product version would read the USB-C 1 meter, which also counts the cell's charging current (the cell not full), so no reading is the board's own. |
| [test103 Whole leak pipeline, end to end](test103-leak-pipeline-timing/README.md) | 2026-11-07 | **later**: not run yet; needs the cameras (until they arrive the K230's job simulates them, |
| [test104 battery energy audit](test104-battery-energy-audit/README.md) | 2026-11-07 | **planned**: when the full firmware runs its battery day for days (HBN sleep between events, the |
| [test106 Wi-Fi setup from the phone, end to end](test106-wifi-setup-from-phone/README.md) |  | **pass on the product pair** (2026-11-02 21:16-21:22 UTC: full firmware in slot 0, setup firmware in slot 1, all |
| [test108 the server drives the check-in and leak check intervals (whole product flow, product firmware)](test108-server-check-schedule/README.md) | 2026-11-07 10:15, run.py 10/10 | **pass**: both intervals follow the server per device within one check-in |
| [test109 secure server (TLS, device identity, pinned certificate, signed change net)](test109-secure-server/README.md) | 2026-10-30 | **planned**: the real server is not built (DESIGN 7, SECURITY.txt 4); it will run on the Windows PC for the test. |
| [test110 setup app recovery from loss at every step](test110-ble-loss-recovery/README.md) | 2026-11-08, run.py, ~4 min | **pass on the product pair and app**: phone Bluetooth off 4 s before the save / during the check and the board reset during the check each end in a named result within 28 s and the app never contradicts the board (Done = stored; "cannot tell whether the network was stored" when the link was lost after the commit); every case is followed by a clean setup, Done 7.8-9.0 s. The test-bed fault cases of 2026-11-03 are in HISTORY.md. |
| [test111 more than one LEAKCAM: one device ID everywhere, presence through the server](test111-multi-device/README.md) | 2026-11-06 09:20, run.py, 150 s; product firmware with the cable setup | **pass, 28/28**: one device ID (7C3E8262B149) on the board, USB and BLE; the app |
| [test113 full setup from the phone app over a USB-C cable (high level, product firmware)](test113-usb-setup-from-phone/README.md) | 2026-11-08 08:14-08:16, the new screens (DESIGN.md 5.3.4 | **pass**: 15/15, Setup open 1.2 s after the plug; |
| [test114 the lowest BLE energy at which the phone still finds the board](test114-ble-discovery-energy/README.md) | 2026-11-01: discovery time per advertising interval; currents not read yet | **part A run**: , parts B and C |
| [test116 the whole setup over BLE alone (no USB)](test116-ble-only-setup/README.md) | 2026-11-03 20:28-20:32 UTC, 16/16, run.py on the product pair, the phone 8 m away in another room | **pass** |
| [test117 presence lost brings BLE back (the phone sets the Wi-Fi at once)](test117-presence-loss-ble/README.md) |  | **pass** (again 2026-11-06 21:56-22:02 on BL616 0.6+g3f9077b/84a2007, 19/19 on USB power: server down -> |
| [test118 the app's icons follow the board (BLE, presence, red)](test118-app-presence-icons/README.md) |  | **pass** (2026-11-01, 7/7; user: "device looses ble, ble icon disappears, looses wifi, wifi icon disappears and we |
| [test120 the BLE low-power setup mode alone (PDS + lpfw advertising, USB wake)](test120-ble-low-power-setup-mode/README.md) |  | **in progress**: BLE sleep with 2 s advertising works on rev 1, the product's BLE runs in it, the phone finds |
| [test121 USB plugged in wakes the board from BLE sleep](test121-usb-plug-wake-ble-sleep/README.md) |  | **pass** (2026-11-01, 2/2 plugs; user: "we can detect usb plugged in? if usb plugged in we switch to 'full' firmware |
| [test122 battery low - the K230 stops at once, the BL616 tells the user to charge](test122-low-battery-notify/README.md) |  | **planned** (2026-11-01, user: "use divider only at k230 to save power. And k230 needs to shutdown quickly in |
| [test123 change the Wi-Fi network remotely, back to the previous one if the new fails in 15 s](test123-remote-wifi-change/README.md) |  | **planned** (2026-11-01, user: "user may want to switch wifi network without going to device, remotely, in case |
| [test124 the BL616's own brown-out protection](test124-bl616-brownout/README.md) | 2026-11-08, run.py, 61 s | **step 2 pass on the product pair**: reset records software `rst=80`, watchdog `88`, exception `88` (it resets through the watchdog; the crash record names it), the last one at the server; step 1 (BOD register) proven on the setup test bed 2026-11-02 (HISTORY.md), no product command for it; steps 3-6 need a lab supply. |
| [test125 setup mode retries presence every 6 h and returns to the full firmware](test125-presence-retry/README.md) |  | **planned** (2026-11-01, user: "after entering setup mode, we still would like to check if presence restored |
| [test126 the slot switch between the full and the setup firmware, both ways](test126-firmware-slot-switch/README.md) |  | **pass, steps 1-7** (2026-11-07 07:50 UTC, 10 round trips on the product pair: full -> setup 4.7 / 8.0 / 8.1 s, |
| [test127 the setup firmware hands over to the full firmware on a USB plug, only with the network checked to the API](test127-usb-handover-to-full/README.md) |  | **pass, steps 1-6** (2026-11-02, user: "usb plug, not only with stored network, but also with checked and once API |
| [test128 crash records reach the server over Wi-Fi as soon as Wi-Fi is available](test128-crash-record-to-server/README.md) |  | **steps 1 2 3 5 7 pass** (2026-11-07 on the DESIGN 5.3.12 firmware, after the interval fix: HISTORY; step 5 |
| [test129 the app shows the board's power and signal as icons](test129-app-power-signal-icons/README.md) |  | **new, not run yet** (2026-11-04, user: "for usb power show plug icon, show battery otherwise; for signal |
| [test130 OTA upgrade of the BL616 full firmware and the K230 image](test130-ota-upgrade/README.md) |  | **BL616 part: steps 1-4, 6, 8 (BL616), 13 (BL616) pass** (2026-11-06, by hand on the bench with the server on |
| [test131 the app's battery icon from every channel (high level, product firmware + app + server)](test131-app-battery-level/README.md) | 2026-11-06 | **partly run**: steps 2 and 6 pass for a board with no reading yet, server only (online over |
| [test132 the leak checks on battery (whole product flow, product firmware)](test132-battery-leak-cycle/README.md) | 2026-11-07 | **not run yet**: needs the hands (USB-C 1 out and in, the probe wet and dry). The HBN sleep |
| [test133 the app's leak alert, emergency style](test133-leak-alert/README.md) | 2026-11-07 18:08 | **pass, 13/13**: locked phone -> alert posted 0.25-1.7 s after the trigger, full screen over |
| [test134 a Wi-Fi change through the server](test134-wifi-change-via-server/README.md) | 2026-11-07 20:16 | **pass, 8/8**: a wrong passphrase refused (0x11), the old network kept, the failure at the |
| [test135 the app's screens, status block, pending list and leak flow](test135-app-screens/README.md) | 2026-11-07 22:26 UTC+2 | **pass, 35/35**: no boards -> Setup 1.5 s, the menus, no search on LEAKCAMs and |
| [test136 every metric in every check-in (product pair: the BL616 full firmware's sensors, the K230's STATUS, the server)](test136-checkin-metrics/README.md) |  | **pass, automated part** (2026-11-07 09:24, run.py 34/34 on the DESIGN 5.3.11/5.3.12 firmware: the K230 on |
| [test137 the K230 leak job with simulated cameras (product pair: the K230 program's job + its link frames)](test137-k230-leak-job/README.md) | 2026-11-07 08:34, run.py 11/11, BL616 full 0.6+g2961d6c-dirty, K230 0.6+g2961d6c-dirty | **pass**: every |
| [test138 time without NTP (product pair: the server -> BL616 -> K230 clock chain)](test138-clock-without-ntp/README.md) | 2026-11-07 08:56, run.py 11/11, BL616 full + K230 0.6+g2961d6c-dirty | **pass**: both clocks within 2 s of |
| [test139 USB-C 1 replug stability (no timing wedges the BLE core)](test139-replug-stability/README.md) | 2026-11-09: A-D 26/26 at 04:19 UTC in 10.4 min, E 8/8 at 04:31 UTC, product pair with the wedge detector | **pass**: no plug timing wedged the BLE core; found asleep after every case in 3.8-6.8 s. |

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
| test114 | Part A holds. Part B (wake beacon) was replaced by the server wake (test119, retired 2026-11-07: no wake, Wi-Fi changes through the server only). Part C is test120. The choice (2 s) lives in the setup firmware, not provision.c. Close it. |
| test120 | Step 7 (USB wake) is done by test121 + test127. Step 5 (full setup from low power) is the test040/106 re-proof. Steps 4 and 6, the battery-side current and the crash-recorder start deadline are still open. |
| test121 | Step 5: with USB in and no network, the setup firmware stays awake (it no longer hands over regardless); the hand-over is test127. README text only; the result holds. |
| test122 | The BLE flag goes in the setup firmware's advertisement. The USB notice (3a) goes through the setup firmware's cable path. The lockout and the flag must survive a slot switch (HBN RAM at a fixed address, like the crash record). Step 6 becomes the switch to setup with the flag. |
| test123 | The failure list adds "API not reachable" (0x19). Otherwise unchanged: full firmware, over the server. |
| test124 | Steps 1-2 done today (record per reset kind). Step 1 needs a read in the full firmware too. Step 5 adds a partition-table write in flight (a slot switch during the drop): the board must boot one slot or the other. |
| test125 | Step 5: a USB plug with presence still lost keeps the setup firmware (test127: the check fails at the API), not "the full firmware takes over". |
| test108 | Rewritten 2026-11-07 for DESIGN.md 5.3.12: the server's checkin/leakcheck intervals per device; pass 9/9. |
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
| Humidity alarm (AHT20 threshold -> report) | dropped 2026-11-06 (test041 removed): the AHT20 is reported only |
| `leakcam_wake` on the board: capture -> history compare (imgdiff + change net) -> `/v1/check` to the mock server -> the sleep it chooses | test103 |
| The model update from the server (per device `GET /v1/devices/{id}/model`, `.new`/`.old`, a bad model) | test130 steps 9-11 (the K230 OTA writer not built; test107 removed 2026-11-07) |
| Session protocol end to end (READY / WAKE / ACK / TIME / SLEEP) with the product agent; NTP time back to the BL616 | test101, test037 steps 1-6 |
| Live stream: both cameras + Opus audio over RTMP to MediaMTX (`leakcam_stream -p`) | test100 step 5 (cameras) |
| OTA: the writer (inactive slot + `ota_meta`) is not built; the SPL's A/B choice never ran on the board | test130 (planned), with the BL616 full firmware too |
| The battery cycle: HBN sleep between the events, DHCP lease reuse, the remembered AP, back-off | test132 (planned: the HBN sleep is not built; the events and intervals pass in test108) |
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
