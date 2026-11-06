# LEAKCAM design and decisions

LEAKCAM is a battery-powered leak camera. It sleeps almost all the time and wakes on a leak probe,
on a timer, or on high humidity. On each wake it photographs the room with two back-to-back
fisheye cameras, decides whether anything changed and whether it is water, keeps a compressed
history on flash, and reports over Wi-Fi.

This file records what was decided and why. Details live in the documents it links to:

| Topic | Document |
|---|---|
| Build and flash, from an empty machine | [firmware/BUILD.md](firmware/BUILD.md) |
| Power management, BL616 firmware, findings | [firmware/README.md](firmware/README.md) |
| K230 board port (boot, NAND, pins, kernel) | [firmware/k230_board/README.md](firmware/k230_board/README.md) |
| Capture, history, streaming (video with audio) on RT-Smart | [attic/firmware/k230_capture/rtsmart/README.md](attic/firmware/k230_capture/rtsmart/README.md) |
| Wi-Fi (BL616 NetHub + K230 SDIO driver) | [attic/firmware/bl616/WIFI.md](attic/firmware/bl616/WIFI.md) |
| Change net | [attic/firmware/k230_nn/README.md](attic/firmware/k230_nn/README.md) |
| LED strips | [LED_STRIPS.txt](LED_STRIPS.txt) |
| PCB stack and gerbers | [LAYERS.txt](LAYERS.txt), [FAB_NOTES.txt](FAB_NOTES.txt) |
| Off-board parts (battery, NTC, cameras, cables) | [EXTERNAL_PARTS.md](EXTERNAL_PARTS.md) |
| Clocks and speeds of every interface (configured, measured, top) | [SPEEDS.txt](SPEEDS.txt) |
| Battery energy budget (sleep, wakes, sessions, battery life) | [ENERGY.txt](ENERGY.txt) |

Status (2026-09-24): first hardware revision, still in design. No board has been made; nothing
below has run on hardware. Firmware claims marked "built" were checked by building and linking.

## 1. Ground rules

These apply to every part of the project.

- **One version.** One firmware image per chip, one boot path, one NAND layout, one pin map. No
  build variants, no configuration switches between alternatives, no board-revision checks in
  firmware. When there is a choice, pick one and delete the other.
- **Lightest runtime, fastest boot.** Only what LEAKCAM uses is built in; SDK features it does
  not use are removed.
- **Fix the source, not the output.** Problems in the design are fixed in the schematic or PCB.
  Fab files (gerbers, drill, pick-and-place, BOM) are regenerated from it, never edited by hand.
- **The schematic is the part list.** Every purchasable part carries its LCSC number in the
  Description field; all BOM and cart files are generated from the sheets. Swaps are only to a
  part with the identical footprint. Fee-free JLC parts (Basic, Promotional Extended) are
  preferred.
- **Rev 1 is the first board.** Hardware findings are changes to make before the first fab.
- **Memory is measured and leaks fail.** Every test records the BL616 (`mem`: free, lowest free, live allocations,
  tasks not yet freed) and the K230 (`free`); every operation the product repeats (session, join, scan, wake, power
  cut) has a leak test over >= 8 cycles (`tools/power-bringup/mem_leak.py`, tests/README.md). No task may keep the
  BL616's IDLE task from running: it frees the memory of deleted tasks (tests/test100, 2026-10-24).

## 2. System architecture

```
 3V3_SLEEP (always on)                        switched by the BL616 (K230_PWR)
 +--------------------------------+          +------------------------------------------------+
 | BL616 (U11, Ai-M62-CBS)        |  UART    | K230D SiP (U3): 2x C908 RISC-V, KPU, 128 MiB   |
 |  - power manager: owns K230    |<-------->|  UART1 IO40/41                                 |
 |    power and reset             |  SDIO    |  MMC0 (4-bit)                                  |
 |  - leak probes (ACOMP wake)    |<-------->|  W25N02KV 256 MiB SPI NAND (OSPI, 3.3 V)       |
 |  - AHT20 humidity every 10 min |  GPIO2   |  2x OV5647 222 deg fisheye (CSI0, CSI2)        |
 |  - wall clock (HBN RTC, Y3)    |<---------|  2x TPS61161 LED drivers (PWM0 IR, PWM1 white) |
 |  - Wi-Fi radio (NetHub bridge) |  hb      |  MEMS mic (codec left input)                   |
 |  - BLE (setup firmware only)   |          |  USB0 on USB-C 1 (flashing), console via BL616 |
 +--------------------------------+          +------------------------------------------------+
            ^ BQ24072 charger, 1000 mAh LiPo, USB-C power
```

### Why two chips

- **The K230D is the camera computer, the BL616 is the always-on part.** The K230D does the
  imaging, encoding and neural networks but cannot sleep cheaply. The BL616 hibernates at a few
  µA and runs the Wi-Fi and BLE radio.
- **The BL616 decides when the K230 runs.** It wakes on its RTC, on a leak probe (analog
  comparator) or on USB plug-in, powers the K230 up, and cuts it off when the session ends.
- **Two BL616 firmwares, one at a time** (5.3.1): the full firmware (`firmware/bl616_full`: power
  manager, Wi-Fi bridge, K230 sessions, **no BLE**) and the setup firmware (`firmware/bl616_setup`: BLE
  provisioning while asleep in PDS, no K230). Whenever BLE is needed the board switches to the setup
  firmware (user, 2026-11-02).
- **Radios only on demand.** Wi-Fi starts only when the K230 sends `WIFI` (or for a check-in); BLE
  only in the setup firmware. A battery session that never asks for Wi-Fi never powers the RF.

## 3. Hardware decisions

| Area | Decision | Why |
|---|---|---|
| View | Two OV5647 Pi-v1.3-format boards with 222° M12 fisheye lenses, back to back (CAM1 J5 top, CAM2 J4 bottom), folded over the board | Spherical coverage is a hard requirement: a leak can be anywhere in the room. Sensors are judged by pixels across the image circle. |
| Camera orientation | The enclosure takes each camera in **both** orientations (rotated 180° about the lens axis): pilots for both hole patterns, a pocket and a flex channel for the connector at either short edge | Rev 1 J4/J5 (bottom contact) need the cameras rotated with the standard 15-to-22 cable, rev 2 reverses the pinout (bugs/bug-005, bugs/v2.txt 10); one enclosure serves both boards and either cable |
| Camera placement | Camera boards (and LED strips) stay at y ≤ 43 mm | 12 mm from the antenna element, about λ/10 at 2.45 GHz |
| Camera clock | No MCLK from the K230; the modules' own 25 MHz oscillator | As on the Raspberry Pi; K230 MCLK pins stay free |
| Camera pin 18 | IO62/IO63, driven low at boot | It is the red status LED on Pi-style boards; it must not light the lenses |
| Storage | W25N02KV 256 MiB SPI NAND, 3.3 V, on OSPI IO14-IO19 | In stock; the SPL/U-Boot built from source know it. Canaan's older k230_sdk only knows the 1.8 V W25N01GW/W25N02JW. |
| IO voltage | Every K230 IO bank on 3V3; only the fixed IO0/IO1 bank is 1.8 V | Matches the BL616's 3.3 V IO. Consequence: Canaan's prebuilt EVB boot loaders (1.8 V banks) must never be used. |
| Boot straps | BOOT0 = 1 (R64 to 1V8), BOOT1 = 0 (R31 to GND): SPI NAND | NAND boot. BOOT1 = 1 selects MMC0, which is the BL616 and never boots, so the ROM falls into USB flashing mode. |
| Forcing USB flash mode | BL616 IO27 → RBT1 10k → BOOT1; test points B1_P/B1_N as manual fallback | The BL616 can put the K230 into USB flash mode without opening the unit (BOOT1 = 1.65 V, reads high on the 1.8 V pin) |
| USB | USB-C 1 = K230 USB0 (flashing, device mode), the enclosure's only USB opening; USB-C 2 = CH340X on UART0 (rev 1, not used since 2026-10-22: **buried in the enclosure, no cutout** - mechanical/enclosure/build_base.py `USB_OPEN`, 2026-10-23) | **The K230 console runs over the BL616 link** ([firmware/CONSOLE.md](firmware/CONSOLE.md)) (UART1, 4 Mbaud): the kernel's link_mux sends console text and link frames on one wire, the BL616 splits them and shows the console on its second USB port (PR1, `/dev/ttyACM1`; `/dev/ttyACM0` = BL616 shell): one USB device for both consoles. The CH340X re-enumerated with every K230 power cycle (it runs from the switched 3V3); its D+ pull-up back-fed VUSB through D3 and kept USB-C sources from starting during a session (bug-002, confirmed 2026-10-25): rev 1 has U2 pins 1-2-3 bridged, **rev 2 drops the CH340X and USB-C 2 entirely** (bugs/v2.txt 3). SPL and OpenSBI print on UART1 too (4 Mbaud: U-Boot ns16550 patched for the clock 1/1 and the fractional divisor, tests/test039); only the BootROM stays on UART0. Boot to app init 0.484 -> 0.30 s from the faster console. |
| Power chain | BQ24072 charger (ISET 0.89 A, ILIM 1.0 A, TMR 6.3 h); TPS62823 0V8 → TPS63802 3V3 and 1V8 → TPS62823 1V1; U7 PG → RSTN | Meets the K230 power-up order (core first, IO after, RSTN 12.5 ms after the last rail). Soft charge chosen on purpose. |
| Power-off | RD1/RD2 1.5 k discharge 1V8/3V3; firmware waits ≥ 300 ms before re-powering; all BL616 pins toward the K230 go analog before K230_PWR drops | The TPS63802 has no output discharge (RD1/RD2 bring the rails below 10 % in < 0.1 s); pull-ups on the switched 3V3 would back-feed the dead rail |
| BL616 pins | PGOOD on IO03 (ACOMP0), K230_PWR on IO30, leak line on GPIO20 (ACOMP1) | Hibernate wakes only on GPIO16-19, the RTC and the two comparators; USB plug-in and leak both need a comparator |
| Humidity | AHT20 on the always-on rail with an RC filter (R52 + C4/C9); polled every 10 min from HBN | No interrupt pin, so polling; the filter keeps Wi-Fi burst ripple off the sensor |
| Clock | Both chips have 32.768 kHz crystals. The BL616 HBN RTC keeps the wall clock; the K230's PMU RTC restarts at every power-up | The K230 RTC supply (AVDD1P8_RTC) is on the switched 1V8 rail. Time is handed over at every boot (section 5.4). |
| Illumination | 4 strips of 36 × 6 mm, each 2 white + 2 IR; white chain and IR chain on separate TPS61161 drivers; domed 3535 LEDs (2.0-2.1 mm) through 3.4 mm holes in the printed cover; strips tilted 30-45° from the camera axis | Light to 2 m; white and IR switch and dim independently; the domes stand out of the cover; the tilt avoids specular glare. [LED_STRIPS.txt](LED_STRIPS.txt) |
| LED current | Maximum: R66 3.0R (white 66.7 mA), R68 1.5R (IR 133 mA), no bench tuning; PWM backs off | A typical unit sits at the driver's switch limit on a nominal battery. TPS61161 (38 V OVP), because the TPS61160's 26 V OVP trips inside the white string's range. |
| Microphone | MSM381ACB026 analog MEMS on the codec left input (MICPL/MICNL), bias from MIC_BIAS through FB4, 1 µF DC blocks; acoustic port 0.50 mm unplated hole | Codec's pseudo-differential input, same topology as Canaan's EVB headset mic |
| Antenna | MIFA on the PCB, feed from module pin 2, simulated (50 Ω, match -25 dB); CT1 shunt pad not fitted; keep-out on all six layers | Re-simulated in CST after layout |
| PCB | JLC06161H-3313, 6 layers, 1.6 mm. L2 and L6 ground, L3/L4 power pours, L5 main signal layer. Regulator ICs bottom, inductors and input caps top behind them | Tight ground reference under both outer layers. The routed state is the plan. [LAYERS.txt](LAYERS.txt) |
| Programming the BL616 | PR1 pads: D+, D-, GND, AI_BOOT, AI_EN; console on USB CDC | UART0 on GPIO21/22 is the K230 link and must stay quiet while the K230 is off |

## 4. K230 boot and system software

### 4.1 Operating system: RT-Smart only

- **RT-Smart, not Linux.** RT-Smart is RT-Thread with MMU-protected user processes and POSIX
  system calls. It boots in a fraction of Linux's time, and its compressed kernel is about
  1.2 MB against tens of MB for Linux and a rootfs. Canaan's camera stack (MPP: VICAP, ISP,
  VENC, audio) and the KPU runtime run on it.
- **SDK:** `kendryte/k230_rtos_sdk` (CanMV manifest). It is installed and patched by
  `firmware/k230_board/install.sh`. There is no Linux build of anything.
- **Build host:** an arm64 host, in a native arm64 Docker image. The SDK's x86-64 toolchains
  run through qemu binfmt.

### 4.2 Boot chain: direct boot, one layout

```
Boot ROM -> SPL (SRAM: DDR training, pad mux and voltages) -> TOC at 0xE0000
         -> slot A or B: OpenSBI + RT-Smart on the big core -> mounts /bin, /sdcard
U-Boot: only when the TOC is missing or neither slot loads
```

- **SPL and U-Boot are built from source** with the LEAKCAM device tree.
  `check_pad_voltage.py` confirms all 64 pads are at their bank voltage. Canaan's prebuilt EVB
  SPL gets 50 of 64 wrong, and a wrong bank voltage can damage the chip.
- **The SPL starts RT-Smart itself** from the TOC, skipping U-Boot, because that is the fastest
  path.
- **The TOC** is 16 entries of 64 bytes: name, offset, size, load flag, boot flag.
  `rtt_a`/`rtt_b` have load = 1 and boot = 0x3 (boot, core 1). The load address comes from the
  image header.
- **A/B slots** (`ota_meta` records, CRC-checked, newest valid version wins; empty means slot A).
  The SPL falls back to the other slot if one does not load. U-Boot is kept only as the last
  fallback.

**NAND layout (256 MiB):**

| Offset | Size | Contents |
|---|---|---|
| 0 / 512K | | SPL (two copies) |
| 896K | 128K | TOC |
| 1M | 256K | ota_meta, in its own erase blocks so rewriting it cannot erase the TOC |
| 2M | 2M | U-Boot (fallback) |
| 4M | 512K | U-Boot environment |
| 5M | 20M | rtt_a |
| 25M | 20M | rtt_b |
| 48M | 8M | UFFS `/bin` (ISP tuning files, 3.3 MB); small because UFFS scans every block at mount |
| 56M | 16M | unused |
| 72M | 32M | UFFS `/sdcard` (programs, image history); small because UFFS scans every block at mount |
| 104M | 152M | unused, bad-block reserve |

**UFFS spare area.** UFFS uses the chip's 64 user OOB bytes. The W25N02KV has 128 spare bytes,
but 64-127 hold its on-die ECC. `mkuffs` and UFFS both cap the spare at 64 for 2 KiB pages, so
the kernel NAND glue is patched to report 64.

### 4.3 Board port

- **One pin table.** `pins.py` generates both the RT-Smart pin mux and the U-Boot pin `.dtsi`.
  Pins that are not connected are inputs with pull-down; none float.

  | Pins | Function |
  |---|---|
  | IO2 | heartbeat to the BL616 |
  | IO7/8 | I2C4, CAM1 |
  | IO14-19 | OSPI NAND |
  | IO38/39 | console |
  | IO40/41 | UART1, BL616 link |
  | IO48/49 | I2C0, CAM2 |
  | IO60/61 | PWM0 IR, PWM1 white |
  | IO62/63 | camera LEDs, held low |
  | IO64-71 | PMU pads, GPIO |

- **Drive strength, set from the 2026-09-24 layout audit instead of series resistors.**
  - OSPI NAND pins (IO14-19): `ds 7` of 15, not Canaan's 15. The traces are short (12-20 mm), there
    is no room for resistors at the balls, and SPI_CLK keeps R42 22R.
  - BL616 SDIO pads: DRV_0 instead of the SDK's DRV_1 (`bl616/wifi_link.c`). Per the BL616
    datasheet, DRV_0 is about 35 ohm on GPIO0-20, close to the ~49 ohm traces, so it
    source-terminates them. DRV_1 is about 11 ohm and rings.
  - K230 SDIO (MMC0 PHY pads): left at Canaan's values (PAD_SP/SN 9/8, TXSLEW 3/1 in `drv_sdhci.c`).
    The code scale is undocumented, and these are the only values known to work at 50 MHz.
    AI_SD_CLK gets a 22-33 ohm series resistor at the K230 instead, if the layout adds one.
- **U-Boot target.** It reuses Canaan's BPI-Zero board code (the same K230D SiP); only the
  device tree and defconfig are LEAKCAM's.
- **Kernel trimmed.**
  - Removed: touch, WS2812, one-wire, Realtek Wi-Fi, USB device, FAT, crypto, FFT, hardware
    timers, soft I2C, unused I2C/SPI buses, regulators, fast-boot preload, CanMV network
    manager.
  - Kept: SDIO0, lwIP, SAL, the WLAN framework and NTP (Wi-Fi). ADC and TS stay because the
    SDK's power-management and audio code link against them.
- **Clock: the PMU hardware RTC** (`RT_USING_RTC_PMU`), set from the BL616 at every boot.
- **Cameras.** CSI0 = CAM2 on `i2c0`, CSI2 = CAM1 on `i2c4`. The OV5647 runs 1280x960 binned
  (full field of view, 45 fps). Both sensors run in VICAP offline mode, because online mode
  takes one sensor only.

## 5. Firmware behaviour

### 5.1 A wake, end to end

1. The BL616 wakes on the RTC (every 10 min for humidity, or the K230's requested interval), a
   leak comparator, or USB plug-in. It powers the K230 and releases reset. Wi-Fi stays off.
2. RT-Smart boots from NAND and starts `leakcam_agent`. The agent sends `READY` on UART1; the
   BL616 answers `WAKE,<reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv>`: the wake reason,
   its clock, the AHT20 sample, the probe node voltage (GPADC on the comparator's pad) and the
   battery as the K230 measured it last session. The agent answers `ACK,<seq>,<bat_mv>` with its
   own reading: the VBAT/3 divider (R59/R61) goes to K230 ADC_1 only and is switched by the
   K230's 3V3, so the BL616 has no battery input of its own and keeps the last value. The agent
   sets the clock and passes the sensor values and the battery to `leakcam_wake`.
3. The agent runs `leakcam_wake <reason>`, the main algorithm (below).
4. The agent sends `TIME` when NTP set the clock and `SLEEP,<seconds>` with the interval
   `leakcam_wake` chose, then stops its heartbeat. 2 s after the last edge the BL616 stops Wi-Fi
   if it ran, cuts the K230's power and hibernates.

**The K230 decides how long a session lasts.** The BL616 wakes it and keeps it powered for as long
as the heartbeat on GPIO2 runs; there is no shutdown command. The agent stops the heartbeat when
its work is done and written; it kills a hook still running after 10 min. The BL616 adds a
backstop for what the heartbeat cannot see (tests/test036, 2026-10-22): a K230 that restarts has
its heartbeat back within ~1 s, and a hung agent whose heartbeat thread still runs never stops it.
So a session that is still running after 15 min (`SESSION_MAX_MS`), or whose agent came up a third
time (`SESSION_MAX_RESTARTS`), is cut and retried / backed off as a failure. A K230 that never sends
`READY` within 10 s (`BOOT_TIMEOUT_MS`; the agent is up 0.9-1.4 s after power-on) is retried and
backed off as a failed boot.

**`leakcam_wake` (`attic/firmware/k230_capture/leakcam_wake.c`):**

```
capture both cameras (LEDs on for the shot, AE settled), reduce to 320x240
  |
compare each camera with what its history shows (cam<N>.hist; none yet = new):
imgdiff, then the change net where imgdiff saw a change (below the camera's threshold = light)
  |-- same or light on every camera, and no sensor alarm ----> sleep 6 h
  |   (once a day a light pair near its threshold is reported as a sample)
  | new, or a sensor alarm (probe wake, humidity alarm, probe node < 1100 mV)
no server configured (/sdcard/leakcam/server) --------------> sleep 1 h
  |
"wifi" to the agent -> WIFI to the BL616 -> MMC0 probe
  |-- refused (no credentials, radio) or no card ------------> sleep 1 h
  | wifi=ok
POST /v1/check?reason=&probe_mv=&bat_mv=&rh=&t=&nn=&thr=&cam<i>=, both reduced frames (+ views)
  |-- server not reached ------------------------------------> sleep 1 h
  |-- {"leak":true}: leakcam_stream -p streams "video":<s> seconds of H.265 + audio live (RTMP)
  |                  from both cameras; nothing stored -------> sleep 10 min
  |-- {"leak":false}: the new frames go to the history ------> sleep 6 h
  |   ("video":<s>: the same stream first, when someone asked for a clip)
  (either answer may offer a change net: GET /v1/nn/<CRC>, stored for the next wake)
```

- **Compared with the history, not with the last wake.** The history is what was last seen and
  judged: a change is new until the server has seen it and said "no leak". Then it becomes part
  of the history, and the next wake compares with it.
- **A reported leak is not stored.** Every following wake (10 min later) sees the change again,
  reports again and sends new video, until the scene is back to normal or the server says "no
  leak".
- **Anything not reported is retried in an hour.** Nothing is stored then either, so the retry
  sees the same change.
- **The sensors count as much as the cameras.** A leak the cameras cannot see (under a cabinet,
  behind the lens' field) still reaches the server when the probes or the humidity say so. The
  server decides from the images and the sensor values together.
- **Wi-Fi only for news.** An unchanged scene with quiet sensors costs a capture and a compare,
  with the radios off.
- **The camera is released before the video.** `leakcam_wake` closes VICAP after the capture;
  `leakcam_stream` opens it again for the push. Both run the same MPP pipeline once.

**Server protocol (HTTP/1.1, one request per connection).** The mock is
`attic/firmware/k230_capture/mock_server.py`; it answers "leak" when the probes say wet (the reason
is `leak`, or the probe node is below 1100 mV), or always / never with `--leak`.

| Request | Body | Reply |
|---|---|---|
| `POST /v1/check?reason=<reasons>&probe_mv=<mV>&bat_mv=<mV>&rh=<%RH>&t=<C>&nn=<CRC>&thr=<cam0>,<cam1>&cam<i>=<state>,<distance>[&sample=1]` (reasons joined by `+`, sent as `%2B`; state first, same, changed or light) | binary PGMs, 320x240, back to back: per camera its frame, then its history view where imgdiff saw a change | `{"leak":true}` or `{"leak":false}`, plus `"nn":"<CRC>","thr":[<cam0>,<cam1>]` when the server has another change net or thresholds, and `"video":<seconds>` when the server wants a clip, for a leak or on request: its length is the server's (the board caps it at 300 s) |
| `GET /v1/nn/<CRC>` | | the kmodel ([firmware/NN.txt](firmware/NN.txt) 3.4) |
| `GET /v1/poll?id=<device>` (the BL616, see below) | | `{"stream":0}`, or `{"stream":<seconds>}` when a viewer asked for live video and audio |
| RTMP `rtmp://<host>:1935/leakcam/cam<N>` (publish, to MediaMTX) | 5 s live: the camera's H.265 from an IDR (Enhanced RTMP) plus the microphone (Opus 16 kHz mono, E-RTMP v2); both cameras at once | the RTMP server's `NetStream.Publish.Start` |

**Live view on request, checked by the BL616 alone (decided 2026-10-25).** Since Wi-Fi is fast (tests/test100: the
remembered AP joins in 0.24-0.47 s, DHCP ~0.6 s, a request 70-158 ms), the BL616 asks the server on its own whether
anyone wants to watch, without waking the K230:

```
RTC wake (poll interval) -> radio up -> join the stored network at the remembered AP -> DHCP (the BL616's lwIP)
  -> GET /v1/poll
     |-- {"stream":0} or no answer -> radio off, hibernate (K230 never powered)
     |-- {"stream":<s>} -> power the K230, WAKE reason "stream": leakcam_stream streams <s> seconds of both cameras
                           with audio (RTMP), then the session ends as usual
```

- **What it costs:** one short radio burst per interval; the energy per check and the resulting interval for the
  battery budget come from tests/test100 (`pt_poll`, the same cycle in the test firmware). The latency a viewer
  sees is up to one interval plus the K230 boot (~2 s) and its join.
- **Split exception:** the BL616's own lwIP serves only this request (as the BLE credential check, wifi_check.c);
  when the K230 runs, it owns the IP address as before (same MAC, so the same lease).
- **The interval is the server's:** each answer may carry `"poll":<seconds>`; the BL616 keeps it in HBN RAM.
- **Measured (test100 `pt_poll`, test firmware, 2026-10-25):** awake **1.71 s** per check from the wake reset:
  radio up 642 ms, association at the remembered AP 240 ms, DHCP 747 ms, DNS + TCP + HTTP 49 ms. About **0.036 mAh
  per check** (times x the measured currents): every 15 min ~3.4 mAh/day, with the 35 uA sleep and the cells'
  self-discharge ~10 months on 2000 mAh before K230 sessions. Radio up + DHCP are 81 % of it.
- **Production design (to build; the test firmware proved the cycle):**
  - *Schedule by time, not by slice count:* HBN RAM keeps the RTC tick each job is due at - humidity sample (10 min),
    K230 report (the K230's SLEEP), server check (the server's interval); every wake does what is due and sleeps
    until the earliest. Replaces today's `hum_wakes_left` slice counter.
  - *Least radio time:* join at the remembered AP; reuse the DHCP lease while it is valid (address, mask, gateway,
    DNS and the lease time kept in HBN RAM; `wifi_mgmr_sta_ip_set()` after a `use_dhcp = 0` connect; a failed check
    drops it); no DHCP ARP check on the BL616 either (`lwipopts_user.h`); the server's IP cached; no disconnect
    before HBN (the radio dies with it). Target ~0.6-0.8 s awake, about half the energy.
  - *Where the server is:* the agent reads `/sdcard/leakcam/server` and sends `SERVER,<host>,<port>` each session;
    the BL616 stores it in easyflash when it changed. No server or no credentials: no checks.
  - *A stream request:* `{"stream":<s>}` starts a K230 session with the wake reason `stream`; `leakcam_wake` takes it
    like an alarm (capture, report), and the server's answer carries `"video":<s>` as for any clip request.
  - *Failures* back off (longer interval, lease and cached IP dropped); the check never blocks a leak or humidity
    wake, which run as before.
  - *Every wake case is tested on the board:* RTC humidity only, humidity alarm, report due, check (no request /
    request), leak wet and dry edges, USB plug, and their overlaps (a leak during a check, a check due with a
    report, a check while USB is in), plus no AP, no server, no credentials.

### 5.2 BL616 power manager ([firmware/README.md](firmware/README.md))

- **Hibernate (HBN) between wakes.** Session flags, the wall clock, the last battery reading and the
  ADC calibration (aon_state), and the event log (evlog) live in HBN RAM, with a magic word and a CRC.
  The linker places them in `.hbn_noinit` (firmware/bl616_common/bl616_leakcam.ld.in: NOLOAD, the top 1 KB,
  clear of the SDK's fixed HBN RAM blocks at +000/+300/+400). `HBN_RSV0` is not usable because
  `pm_hbn_mode_enter()` overwrites it.
- **Sleep current, measured once (tests/test031, 2026-10-25, meter in the cell lead; not to be repeated):** the
  product's sleep (HBN level 0, leak + USB comparators armed) **35-36 uA**. The comparators cost ~3 uA each (RTC
  alone 29 uA); RC32K off and a lower HBN LDO (0.90 V) change nothing; **HBN level 1** saves 3 uA but **loses HBN
  RAM**, so the product stays on level 0. The rest (~26 uA) is the board: U5 TPS63802 11 uA + its feedback divider
  5.5 uA, BQ24072T 4.3 uA, the probe divider 1.65 uA. No pin pull costs anything in HBN (the core domain is off:
  every non-AON pad is high-Z; every external pull sits at its own rail). Decision: keep the USB-plug comparator
  (3 uA, ~2 mAh/month) so pairing starts at once. Rev 2: a nano-amp regulator for 3V3_SLEEP (bugs/v2.txt).
- **HBN RAM rule (tests/test021, 2026-10-22).** While HBN_SRAM has retention (RET) or sleep (SLP) mode
  set, an HBN RAM access right after any AON register write is corrupted (4-12 % of them, both reads
  and writes; none with the mode off). The mode survives every wake and reset. So `aon_init()` turns
  both bits off before the first access, and `aon_prepare_sleep()` sets RET as the very last step in
  `hbn_sleep()`, after every HBN RAM write (nothing may touch HBN RAM after it).
- **ADC calibration kept across HBN.** `bflb_adc_init()` calibrates once per reset (28.6 ms) and every
  wake is a reset; the cold boot's result is stored in aon_state and handed back through bflb_adc.c's
  globals: the first probe reading after a wake 31.2 -> 2.6 ms (tests/test021).
- **Wake sources:**
  - leak line on ACOMP1, trip at 1.10 V (vio_sel 22, test034 2026-10-31: between a pressed wet towel and a dried salt film; the scaling factor has no effect, tests/test034);
  - USB plug-in via PGOOD on ACOMP0;
  - the RTC.
- **The BL616 does not judge.** It keeps no memory of what was reported: a probe reading wet or
  humidity at or above 85 %RH at any wake starts a K230 session, every time, and the values
  ride in `WAKE`. The K230 decides leak or no leak and how long to sleep. The only state the
  BL616 keeps is the failed-boot count (for the retry back-off) and the USB-exit flag.
- **USB mode.** On USB power the full firmware stays awake (60 s check-ins, 5.3.1); it has no BLE.
  Provisioning over BLE is the setup firmware's (5.3, 5.3.1). The phone side is the Android app in
  [android/](android/README.md): SSID and password on one screen. Before it stores them the BL616
  checks them itself: it joins, gets an address, reaches a well-known internet URL and then the
  LEAKCAM API (the presence server answers a check-in: the end goal, 5.3.1), and the app shows
  which step failed. Unplugging returns it to the battery schedule.
- **No recovery of its own.** A stopped heartbeat means the session is over, whatever the
  reason: the BL616 cuts the power (300 ms discharge wait included) and schedules the next wake.
- **Console on USB CDC.** It requires the SDK shell, FreeRTOS and CherryUSB CDC-ACM. If any of
  these is missing, Kconfig drops the option silently and the console lands on the K230 link
  pins. Check `CONFIG_BSP_CONSOLE_USB_CDC` in the generated config.

### 5.3 BLE: Wi-Fi credentials only, in the setup firmware only

- **The full firmware has no BLE at all (user, 2026-11-02):** no BLE stack, controller or advertising windows. Each
  time BLE is needed (no network stored, presence lost, a phone asks for setup) the board switches to the setup
  firmware (5.3.1). The full firmware gets simpler: its Wi-Fi is only the NetHub bridge and the check-in, no BLE
  coexistence, no credential check during a K230 session, and the BLE controller's 32 KB exchange RAM goes back to
  Wi-Fi.

- **One GATT service:** SSID, passphrase and commit (write only), and STATUS (read and notify: the
  check's progress and result).
- **Authenticated link: LE Secure Connections passkey entry (user, 2026-11-01).** Each board holds its
  own 6-digit PIN, pre-stored when its firmware is flashed (`make flash` runs provision_pin.py); only
  the flashing host and the presence server know how it is derived, and the app asks the server for it
  right before pairing and enters it itself. Just Works is refused, so a phone without the app cannot
  pair. The setup firmware advertises from its start, every 2 s asleep in between, every 100 ms while awake on USB
  power or after a phone's wake (5.3.1, test106 2026-11-02). The SDK's
  profile for our controller library needs `sdk_patch.py` for Secure Connections at all;
  `CONFIG_BT_FIXED_PASSKEY=y` for the PIN.
  Threats, findings and open items: [SECURITY.txt](SECURITY.txt).
- **A phone that does not pair is dropped (user, 2026-11-04):** any phone in range can open a link to a connectable
  board, so one that has not paired with the PIN 10 s after it connected is disconnected, and one whose pairing fails
  at once; a neighbour cannot hold the board's one connection (ble_pairing.c auth_task_fn; tests/test040 step 2).
- **Stored in easyflash** (`wifi_ssid`, `wifi_psk`), and nothing else.
- **No pairings kept (user, 2026-10-29).** No bond keys in flash (`CONFIG_BT_SETTINGS` off): every
  setup pairs anew; after a reset or sleep the board has forgotten the phone. Nothing to
  steal or wipe; the credentials are written once per installation. The app removes the phone's old
  bond itself before every send and pairs anew (2026-10-30); only a phone that refuses the removal
  gets the request to Forget the board by hand (`err_unpair`).
- **No other BLE features:** no shell, OAD or device status. Everything else goes over Wi-Fi. The one addition
  (use case 2, user 2026-11-01): the advertisement carries the device ID (manufacturer data, company 0xFFFF: the
  BL616's factory MAC, device_id.c), so the app tells a LEAKCAM it set up before from a new one; the same ID is the USB
  serial number and goes with every server check-in (tests/test111).
- **No K230 during BLE.** The setup firmware never starts the K230, so a BLE check never competes with a session
  (the full firmware's former "BLE first" rule and the session restart on new credentials, tests/test040 step 8, are
  gone with its BLE). Credentials checked and stored -> the switch to the full firmware, which joins them.
- **Switching networks never waits on the old one.** After a failed join (a network that is gone, a
  changed router password) the Wi-Fi supplicant keeps retrying, and a connect sent into those
  retries never returns. Every join and every check first leaves until the supplicant is idle
  (`wifi_link_sta_leave()`: disconnect each second, at most 10 s). The new network is joined 2.2-2.5 s
  after the store following a missing network, 5.1 s following a changed password (test040).
- **BLE start keeps USB alive.** The SDK's BLE controller resets the PDS block when it starts (weak
  hook `btblecontroller_software_pds_reset`); the BL616's USB PHY is controlled from PDS. A/B on
  the test firmware (tests/test040, 2026-10-29): with the SDK's reset the USB device dropped at
  `pt_ble on` and never came back (EN needed); with `ble_pairing.c`'s empty hook BLE started dozens
  of times in runs 12 and 13 with USB kept. The empty hook stays; BLE sleep in PDS works without it (test120).
- **Wi-Fi RAM.** The BLE controller library keeps 32 KB of the 160 KB Wi-Fi RAM as exchange memory and lpfw 30 KB:
  both only in the setup firmware, which has no NetHub. The full firmware's 26 NetHub buffers no longer share it
  with BLE (its 6 KB own lwIP heap, `lwipopts_user.h`, tests/test040, can grow).

### 5.3.1 Two firmwares: setup (no presence, BLE in low power) and full (user, 2026-11-01)

The BL616 runs one of two firmwares, each an image in its own flash slot. **All setup activity runs in the setup
firmware, the normal product run in the full firmware** (user, 2026-11-02): BLE and the USB cable setup are the setup
firmware's; check-ins, K230 sessions, leak alarms and uploads the full firmware's. The setup firmware is there so the
app can always reach a board that is not online.

| | Setup firmware (firmware/bl616_setup) | Full firmware (firmware/bl616_full) |
|---|---|---|
| Runs while | setup: no network stored, the last check-in failed, or the app asked for setup | the normal run: presence works (on battery or USB power) |
| BLE | advertising every 2 s while the chip sleeps in PDS (100 ms while awake); a phone sets the Wi-Fi | **none** (user, 2026-11-02): every BLE need switches to the setup firmware |
| Wi-Fi | only the credential check after a phone sends a network (BLE or cable) | check-ins, the K230's bridge (NetHub), the remote network change (5.3.2: over the server, needs presence) |
| K230 | only as the USB cable relay while a phone is plugged in (below), never a session | sessions as before |

**Switching (slot switch: mark the other slot active and reset, ~1 s):**
- Full -> setup, **each time BLE is needed** (user, 2026-11-02):
  - the first failed check-in, or no network stored (tests/test117);
  - a phone asks for setup through the server (the app's "set up again" on a board online, test119): the check-in
    answer says so and the full firmware switches.
- Setup -> full:
  - USB-C 1 plugged in (PGOOD wakes the chip from PDS, tests/test121) **and the stored network passes the check up
    to the API** (user, 2026-11-02): the setup firmware runs the network check on the stored network first, and only
    a pass switches to the full firmware (on USB power: 60 s check-ins, K230 sessions). No network stored, or the check
    fails at any step: it stays in the setup firmware (setup is needed), awake while on USB power, offers BLE and the
    cable setup, and reports the failed step (BLE STATUS, the cable, the console).
  - New credentials saved over BLE or the cable: they are stored only after the same check passed up to the API.

  **The network check's end goal is the API reachable** (user, 2026-11-02): join -> address (DHCP) -> internet (the
  well-known URL, generate_204) -> **API** (a check-in answered by the presence server, server/). The internet step
  stays so a failure names the right cause: no internet (the network's fault) or API not reachable (the server's, or
  a network that blocks it). wifi_check.c already runs with a target (presence.c's check-in uses the server); the
  credential check gets the API as its last step and STATUS a new failure "API not reachable". Code, BLE STATUS and
  app: to do (tests/test040 and test106 re-prove it).
  - Every 6 h, for a retry (tests/test125): the full firmware tries its normal check-in. Reached: presence is back
    and it stays. Not reached: back to the setup firmware at once. The setup firmware has no check-in of its own.
- Partition-table writes: two per 6 h retry, 8 a day, ~3000 a year, far inside the flash's endurance.

**Setup over the USB cable is the setup firmware's too** (user, 2026-11-02; tests/test113). On rev 1, USB-C 1 is the
K230's USB0, so the setup firmware powers the K230 with a small relay image while the phone is plugged in and no
network is stored: phone (OTG) -> K230 USB0 (CDC ACM) -> link UART -> the setup firmware's provisioning (the same
operations and STATUS as over BLE, the same PIN). No session, no NetHub, no camera: the K230 is off again when the
cable is pulled or the credentials are saved. The setup firmware gains k230_power.c and the link UART framing
(k230_link.h); its RAM has room (no NetHub). Rev 2 option (bugs/v2.txt): the BL616's own USB on USB-C 1, no K230.
A configured board set up again over the cable: the app asks the full firmware (through the server, or over the
cable to the K230 in its session) and it switches to the setup firmware.

**Check-in intervals (full firmware):** the server gives the interval in each check-in's answer, by the power the
check-in reports (`usb=1|0`): **60 s on USB power, 24 h on battery** (~0.036 mAh per check-in). The server judges a
board offline after three of its own intervals missed: 3 min on USB, 3 days on battery. Plugging or unplugging USB
checks in at once, so the new interval applies right away. On battery, what rides on the check-in waits for it, up to
24 h: a phone's wake through the server (test119) and a remote Wi-Fi change (5.3.2). A leak alarm and the battery-low
notice (test122) check in at once.

**Setup firmware: only what setup mode needs** (firmware/bl616_setup/README.md):
- BLE provisioning: the same ble_pairing.c and wifi_check.c as the full firmware. Its Wi-Fi is the BL616's own station
  (wifi_setup.c, the wifi_link.h interface without the bridge).
- Low power: the SDK's low-power firmware (lpfw, `CONFIG_LPAPP`, from examples/pmu/wl_ble_lp), PDS between
  advertisements, the RC32K with the SDK's auto calibration (no Y3 needed).
- A phone connecting keeps the chip awake (pairing, check, save); when the phone leaves it sleeps again.
- The USB cable setup: the K230 powered with the relay image only while a phone is plugged in (k230_power.c, the
  link UART; tests/test113, to come).
- The watchdog, the crash recorder, the USB console (`isp` for flashing).

**Why two images, not one:** the low-power firmware takes 30 KB of the Wi-Fi RAM (lpfw_share), which does not fit next
to NetHub (bl616_powertest + `CONFIG_LPAPP`: ram_wifi over by 30144 bytes).

**Slots:** the SDK's partition table as it is: boot2's FW entry with slot 0 (full, 2 MB at 0x10000) and slot 1
(setup, 1.41 MB at 0x210000); both images ~1.3 MB. The SDK's mfg partition overlaps slot 1; nothing writes it. The
switch (firmware/bl616_common/slot.c, `slot switch`) flips the FW entry's active index into the other partition-table copy
with a higher age and resets. Flashing writes both copies (firmware/bl616_common/host.mk; `SLOT1_BIN=` adds the setup image):
after a switch the newer copy otherwise still names the other slot. Shared by both images: the settings in easyflash
(credentials, PIN, clock source), the crash record at one fixed address, the watchdog and `isp`.

**One product version for both images** (user, 2026-11-02): firmware/VERSION; each build stamps it with the git
commit (and the build time on a tree with uncommitted changes) as "leakcam-build <image> <version>", printed at boot
and by `slot`. The full firmware's `make flash` writes the pair (the setup image into slot 1) and refuses two images
of different versions or commits. The crash record keeps the image and version that wrote it: the other image may be
the one that reads it after a switch, and decoded with the wrong ELF the addresses name wrong but plausible code
(tests/test126); every flashed ELF is kept by its build (~/.cache/leakcam-elf), and crash_decode.py picks it. OTA writes the
inactive image and checks it before switching (no A/B fallback); the ROM's USB ISP stays the recovery. The switch
alone, both ways: tests/test126.

**Rules for the setup firmware's sleep** (tests/test120; skill bl616-ble-low-power):
- **Never enter the low-power sleep path while BLE is up but idle** (no advertising, no connection). The SDK asks the
  BLE controller for its next deadline before any veto. With none, the BLE core went to sleep for good, and after any
  software reset (reboot, ISP, watchdog, POR or system reset) its init hung forever. Only the EN pin cleared it, which
  a board in a pipe cannot get. The idle hook checks `ble_pairing_sleep_ok()` first.
- The BLE controller's PDS reset stays off (it drops USB); BLE sleep works without it.
- The low-power board init gates the USB and watchdog clocks: the firmware enables them, and again after each wake.
- USB is detached once for the whole sleep, not around each advertising wake. The watchdog's feeding holds while
  asleep; each wake restarts its counter, so an app hung while awake is still reset.
- Measured on rev 1 at 2 s advertising: 99.4 % of the time in PDS, ~12 ms awake per advertisement; the phone finds the
  sleeping board 10/10, median 1.1 s, max 3.7 s. The battery current is measured on the next assembled board (the
  vendor's 428 uA until then). At 2 s the phone's 5 s connect limit runs out now and then: a setup on battery takes
  16-23 s at times, without a dialog (test106, 2026-11-02; user: 2 s kept).

**The battery's lowest voltage: the cell's protection board, ~2.4 V** (user decision; bug-004, accepted):
- The 503450 cell (34 x 50 x 5 mm, 1000 mAh) has a built-in protection board (EXTERNAL_PARTS.md; typically a DW01-type
  IC with an 8205 dual FET). It cuts discharge at ~2.4 V and lets charging back in.
- The BQ24072 has no battery undervoltage cutoff (datasheet SLUS810K: its UVLO is on IN, the USB input; on battery
  OUT is connected to VBAT, with only a short-circuit trip).
- When the cell is cut, 3V3_SLEEP collapses; the BL616's brown-out (enabled by the SDK's startup: 2.40 V, reset) keeps
  the chip from running erratically on the way down. The next USB plug recharges it (precharge below ~3 V).
- So the BL616 needs no battery reading (no divider, not in rev 2 either), and the setup firmware has no cutoff of its
  own. A board that waits out its whole charge goes to ~2.4 V, which costs the cell some cycle life.
- To confirm on the next assembled board: test038 step 7 (where the drain really stops) and test124 (the BL616's
  brown-out alone).

**Battery low: the K230 measures, stops at once, the BL616 tells the user** (tests/test122):
- The battery divider is only on the K230 side (R59/R61 through Q1, on the K230's rail): nothing while the board
  sleeps. Every session reads VBAT (ADC_1) and reports it in WAKE's ACK.
- Below 3.10 V under load (~3.2 V at rest, ~2-3 % of the charge left), the K230 ends its session at once (no upload,
  no new capture; a write in flight finishes). It is the big load, so stopping it keeps the charge for the notice.
- The BL616 keeps the battery-low state (flag + last VBAT) in HBN RAM (aon_state: survives every sleep and reset, lost
  only with the battery), and reports it on every channel:
  - Wi-Fi: one check-in with the flag right after the cut; the server keeps it and the app shows "battery low, please
    charge" (also a phone notification when it can wake the app).
  - BLE: the flag in the advertisement while it advertises, and in STATUS once connected.
  - USB: the console status (`provision`) and the USB setup path's status.
- Then the lockout: no K230 session, no RTC or leak wake (a leak alarm cannot run on an empty cell); only the USB plug
  wakes it. It ends with USB power and the next reading >= 3.50 V (test038). The charge left lasts weeks of lockout
  before the cell's protection.

**Built (2026-11-02):** the full firmware has no BLE: no network stored, a failed check-in or "ble":1 switch it to the
setup firmware, with the reason in easyflash (`setup_why`: "lost", "wake"). The setup firmware reads and deletes it at
its start: after a wake it goes back in 2 minutes, otherwise every 6 h while a network is stored (firmware/bl616_setup/
switch_to_full.c); never while a phone is connected. Coming from the full firmware with USB power in is not a plug: no
hand-over check then. To prove on the board: tests/test117, test119, test125.

**Tests:** test120 (BLE in low power alone), test121 (USB plug wake), test125 (the 6 h retry and the whole
full -> setup -> full cycle), test122 (battery low), test124 (BL616 brown-out), test038 step 7 (the real cutoff); in
setup mode: test116 (BLE only), test113 (USB only, in the full firmware), test117 (presence lost -> setup),
test106/test111 (the app's setup and list).

### 5.3.2 Changing the Wi-Fi network remotely, with fallback (user, 2026-11-01; tests/test123)

The user can move a working board to another network from the app, without going to it (a second network nearby).
The change goes through the presence server and the board's own check-in, so it needs presence (operational mode):

- **The app** sends the new SSID and passphrase for the device to the server; the server holds it as a pending change
  with the app's attempt id.
- **The board** gets it in the answer to its next check-in (the check-in interval, or at once when the phone wakes it
  through the server, test119), and runs the same credential check as over BLE (wifi_check.c: join, address, then a
  check-in at the server over the new network) with a **15 s budget**.
  - Works: the new network is stored, the old one kept as `wifi_*_prev`; the result (attempt id, saved) goes with
    that check-in.
  - Fails or 15 s pass: back to the previous network (stored, still the current one), a check-in there with the
    result (attempt id, the failure: wrong password, network not seen, no address, no internet, API not reachable);
    nothing stored.
  - Lost in between (power, reset): the stored network is still the old one, so the board comes back on it; the app
    reads the result by attempt id, or "not done" (the same reconciled state as BLE's STATUS, test110).
- **The app** shows the outcome by attempt id: moved, or kept on the old network with the reason.
- During a K230 session the change waits for the session's end (the bridge's association is the session's).
- **Open (security, SECURITY.txt):** the passphrase must not cross the board's plain-HTTP check-in or rest at the
  server in clear: encrypt it with a per-device key derived like the pairing PIN (the server and the flashing host
  hold the secret), and the server drops it once delivered. And a remote change needs the app authorized for that
  device (today the device ID alone would do), or anyone knowing an ID could move a board onto their own network.

### 5.3.3 Crash records go to the server over Wi-Fi (user, 2026-11-02; tests/test128)

A crash record (crash_rec.c: kind, task, registers, backtrace, the build that wrote it) is kept in HBN RAM until the
server has it. It is sent **over Wi-Fi only, as soon as Wi-Fi reaches the LEAKCAM API**, by whichever firmware gets
there first: the full firmware with its next check-in (at boot it checks in at once when a record is kept, not after
a 24 h battery interval), the setup firmware with its network check's API step (a BLE setup, the USB hand-over).
Not over BLE or USB: the console still prints it at boot and with `crash`, as today.
- **State, not events:** the record carries its build and count; the server stores it per device, keyed by the
  whole record text (the count starts at 1 again after every send, so (build, count) is no key), so a resend is
  harmless. The board clears the record only after the server's answer; a lost answer only sends it again next time.
- **A new crash before the old one was sent** overwrites it (one record, the count tells how many were lost).
- **Server:** POST /v1/devices/{id}/crash (a url-encoded form: build, count, kind, task, registers, up to 16 backtrace
  words; the server rebuilds the text `crash` prints); GET
  /v1/devices/{id}/crashes for the bench and later the app; the host decodes with the kept ELF of that build
  (crash_decode.py, ~/.cache/leakcam-elf). The record holds no secrets (no credentials, no PIN).
- **Cost:** one extra HTTP request per crash, nothing when there is none.
- **The K230 too** (user, 2026-11-05; skill leakcam-product-firmware section 0): the K230 product programs get the
  same recorder (kind, thread, registers, backtrace, build) in memory that survives its reset, sent to the same
  endpoint the next time the K230 runs a session with Wi-Fi up. To build; until then a K230 fault shows only on its
  console.
- **Fail fast:** a fatal error on either chip ends the program or resets the chip at once, with one log line naming
  it and the record written; no silent retries (product application standard). A K230 failure ends in power off: its
  heartbeat stops and the BL616 cuts the rails 2 s later and logs it (user, 2026-11-05: battery).

### 5.3.4 The setup channel and the app's link icons (user, 2026-11-04)

- **The most reliable channel that is there:** a USB link between the app and the board (the phone's USB-C to the
  board's USB-C 1 by cable, the phone the USB host; user 2026-11-04: the phone app, not a computer) -> the setup goes
  over USB; else over BLE; over Wi-Fi (5.3.2, through the server) only for a board
  that is already set up and moves to another network.
- **The app shows the link and the board's power** next to each board:
  - a USB icon (as the BLE icon) only while the app holds a USB link to the board - not for USB power alone;
  - a plug icon when the board has USB power but no USB link with the app;
  - a battery icon otherwise.
- **Signal strength as bars** (BLE and Wi-Fi), never the dB value on the screen; the dB value on a long press (the
  phone's "hover").
- Open, before the USB setup is built: the protocol over the USB link (a CDC port of its own, not the shell: no
  credentials on the console), the phone side (Android USB host, OTG; the permission dialog to keep to one,
  the fewest system dialogs).
- The board's USB power reaches the app in the BLE advertisement: the flags byte's bit 1 (bit 0: a network stored),
  read when advertising starts (an unplug restarts it; a plug-in switches to the full firmware).
- Tests: one case per rule (the channel picked with a USB link, without it, for a set-up board; each icon; bars and
  the long-press value).

### 5.3.5 A board nobody uses: setup mode and a daily leak check (user, 2026-11-04)

- **The server is the one source of truth; clients are views (user, 2026-11-04):** the app is only one way in, a web
  interface may follow. Users, their boards and every state live on the server; a client reads and changes them only
  through the server's API and keeps nothing of its own. The registered user is the client device (today the phone by
  its Android ID); a web client registers through the same API.
- **The server holds the list, the app none (user, 2026-11-04):** the app keeps no list of its own; it always asks the
  server for the boards registered to its phone (`GET /v1/phones/<phone id>/devices`) and shows them with their
  presence. A setup registers the phone (below); nothing about the boards is stored on the phone.
- **Deleting a board removes this phone only:** someone else may still use it. The delete goes to the server; the app
  shows the server's list again, without the board. It does not come back by itself; only a new setup brings it back.
- **No user left:** when no phone has the board any more (nobody else set it up, nobody receives its leak reports),
  the server answers the board's check-in `410 Gone`. The full firmware then switches to the setup firmware (reason
  "unused"): ready to be set up again, and checks for leaks only rarely, every 24 h. With a single user (the bench,
  tests/test111) the delete alone sends the board back to setup.
- The parts, in build order:
  1. App: no stored list (devices.xml goes): the list is the server's `GET /v1/phones/<phone id>/devices`; a delete is
     `DELETE /v1/devices/<id>/phones/<phone id>`, then the list read again (no server: nothing changes, the app says
     why). The phone id is the phone's Android ID (`Settings.Secure.ANDROID_ID`: one per phone and signing key, the
     same after a reinstall). The app's "rediscover" (a deleted board put back by itself) goes.
  2. Server: the registered phones (the users) of each board. A phone is registered when the server issues it the
     board's pairing PIN (`GET /v1/devices/<id>/pin?phone=<phone id>`: every setup asks for it), removed by its
     delete; none left -> `410 Gone` to the check-in. `GET /v1/devices/<id>` tells the number of phones ("phones").
  3. Full firmware: `410` -> switch_to_setup("unused").
  4. tests/test111 step 7: delete -> the board in the setup firmware within one check-in interval.
  5. Later (needs the K230 in the setup firmware): the 24 h leak check, and what a leak found then does with no one to
     tell.

### 5.3.6 A leak without presence: one record kept until it can be sent (user, 2026-11-04)

- **Leak detection runs in the full firmware whatever the presence:** the periodic leak check goes on while the
  server is out of reach (and in the "unused" setup mode, 5.3.5, every 24 h).
- **A leak found without presence is stored in flash, one record** (the newest; one data point is enough):
  - the trigger type (probe comparator, humidity, picture change, ...);
  - the humidity and the temperature (AHT20);
  - the probe voltage;
  - a picture reference id: the picture stays on the K230's storage, uploaded from there later.
- **Presence back -> the record goes out** with the first check-in (then the picture from the K230 by its id) and is
  deleted from flash only once the server confirmed it.
- **Presence does not flap:** it is lost only after several missed check-ins in a row (today three), not on one
  failed request; a short outage never starts the setup firmware or the offline path.
- Open, before it is built: the record's flash key and format (easyflash, fixed size), the K230's picture store and
  its id, the server field for the record.

### 5.3.7 Firmware upgrades over the air, from the server (user, 2026-11-05; tests/test130)

- **What:** the K230 image and the BL616 full firmware. The BL616 setup firmware is not upgradeable over the air: it
  changes only by wire (`make flash`).
- **Who starts it: the server, automatically.** The check-in answer names the newest version. A board on an older
  one downloads and installs it by itself: no prompt, no app, no user action.
- **Checksum verified:** the server gives each image's checksum. The board checks the written image against it before
  switching. A mismatch, a broken download or a power cut mid-write keeps the old firmware running, and the next
  check-in retries.
- **After the switch:** the board reports its new versions at its next check-in.
- **The BL616 full firmware is written by the setup firmware** (user, 2026-11-06): the 4 MB flash has no room for a
  second full copy (slot 0 2 MB = full 1.09 MB, slot 1 1.44 MB = setup 1.29 MB). The full firmware switches to the
  setup firmware with reason `ota`; the setup firmware downloads the new full image straight into slot 0, checks its
  SHA-256 and switches back. A broken download or a power cut leaves the board on the setup firmware (Wi-Fi and the
  server check-in work there), which retries; the old full image is gone by then.

### 5.3.8 One K230 product image; the BL616 picks the program at the start (user, 2026-11-05; tests/test113)

- **One image, no image switching.** The K230 boots the same product image every time; its product program starts at
  boot (`RTT_AUTO_EXEC_CMD`) and asks the BL616 what to do.
- **The parameter is the WAKE reason**, the answer to the program's READY (5.4, k230_link.h): `setup` = the USB setup
  relay (USB-C 1 CDC ACM <-> the link to the BL616's provisioning; no camera, no Wi-Fi on the K230); the other
  reasons (cold, leak, rtc, usb, humid) = the normal session. No extra pin, boot argument or flash flag: the question
  is asked over the link the session needs anyway, and the BL616 owns the decision (it knows VBUS, the stored
  network and why it powered the K230).
- **Who sends `setup`:** the setup firmware, when USB-C 1 has VBUS and no network is stored (the phone plugged in to
  set the board up). The full firmware never does.
- **The product kernel carries the USB device** (CherryUSB, CDC ACM on USB0) for this; the phone lists it 1.41 s
  after the K230 starts (tests/test044).

### 5.4 K230-BL616 link and time

- **The UART carries session control.** UART1 at 2 Mbaud 8N1 (stable; 4 Mbaud lost short writes, tests/test039), frames `$<seq>,<CMD>[,args]*XX`
  with a CRC-8 (polynomial 0x07, `link_crc8()` in `k230_link.h`, shared by both ends); invalid
  lines are dropped. It is available as soon as RT-Smart runs, without Wi-Fi.
- **Every command is answered.** `ACK,<seq>` means accepted; `NAK,<seq>,<code>` means received but
  refused, with a reason: 1 invalid argument (a `TIME` before 2026), 2 no Wi-Fi credentials
  stored, 3 BL616 radio failed. The agent never refuses `WAKE`: sensor values out of range are
  dropped instead, so a bad reading cannot stall a session. The sender keeps one command
  in flight, resends it after 300 ms without an answer, and gives up after 5 sends. A NAK ends the
  command at once. A corrupted frame gets no answer, because its seq can't be trusted, so the
  timeout covers it. Repeats (a lost answer) get the same answer again and are not acted on twice.
  A `READY` restarts both sequence states, unless it repeats the one just accepted. The timeout,
  retry count, result names and NAK codes are defined once in `bl616/k230_link.h`, which the agent
  includes too.
  - K230 → BL616: `READY`, `SLEEP,<s>` (the next wake only, not a power-off), `TIME,<unix s>`,
    `WIFI`.
  - BL616 → K230: `WAKE,<reasons>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv>`, reasons being
    every cause that applies joined by `+` (`cold`, `leak`, `rtc`, `usb`, `humid`; e.g. `rtc+leak`)
    (rh -1 = the AHT20 read failed, probe -1 = no reading, bat -1 = no reading since the power loss),
    once per `READY`, answered `ACK,<seq>,<bat_mv>` with the K230's ADC_1 reading; `ACK`.
- **K230 side: `leakcam_agent`**, an RT-Smart program started at boot (`RTT_AUTO_EXEC_CMD`). It
  talks to `/dev/uart1` and `/dev/gpio` directly and runs the capture hook for the wake reason.
  The hook prints `wifi` when it needs the network; the agent sends `WIFI` and, once it is
  accepted, probes MMC0 through `/dev/bl616`. Nothing probes MMC0 at boot: the SDU is off until
  `WIFI`, so a boot-time probe would only cost time and power.
- **The program reads frames from `/dev/link`** (the kernel's link_mux splits UART1 into the console and the frames,
  firmware/CONSOLE.md 4): `poll()` then `read()`. A driver's own `dfs_file_ops` must be attached after
  `rt_device_register()`, which clears them: before 2026-11-05 they were set first, devfs's generic `poll()` never
  reported data and no K230 program had ever received a frame (the old agent's READY went unanswered as well) -
  tests/test045 proves the path end to end, and `link_stat` shows `/dev/link fops attached`.
- **GPIO2 heartbeat.** Every 500 ms while the agent runs; its stop ends the session. It is
  edges, not a level, because IO2 is JTAG_TCK with a pull-down at reset.
- **Time.** The BL616 is the clock of record. Its HBN RTC is a 40-bit counter on the Y3 crystal
  plus a (Unix seconds, count) reference, re-based at every boot. It drifts about 1.7 s/day.
  - The time rides in the `WAKE` reply (`0` = not valid), so the K230 has it in the first frame
    it receives, and sets its PMU RTC from it.
  - When the K230 has NTP time over Wi-Fi, it sends `TIME,<unix s>` back.
  - The clock is lost only with the battery. `WAKE` then carries 0 until NTP time arrives
    again.

### 5.5 Wi-Fi ([attic/firmware/bl616/WIFI.md](attic/firmware/bl616/WIFI.md))

- **Split.** The BL616 is only the radio and the WPA supplicant. The K230's lwIP owns the IP
  address: DHCP, DNS, NTP and every socket, with the BL616's station MAC.
- **Why this split.**
  - It is the split Bouffalo's own host driver uses.
  - It keeps one IP stack.
  - Streaming and uploads come straight from the K230.
- **BL616 side.**
  - NetHub on the SDIO profile.
  - Its default receive filter (which keeps DHCP and ICMP on the BL616) is replaced: only EAPOL
    stays, every other frame goes to the K230.
  - It joins with `use_dhcp = 0`.
- **K230 side.**
  - A new RT-Smart driver.
  - SDIO transport to the BL616 SDU (424c:0606, 4 ports per direction, CMD53 in 512-byte
    blocks).
  - The NetHub message router with per-channel handshake and download credits.
  - One worker thread woken by the card interrupt, plus a 1 s safety poll.
  - Fixed transmit rings; `wlan0` registered with the RT-Thread WLAN framework.
  - It is a clean reimplementation: Bouffalo's Linux host driver is GPL.
- **Control messages** (status, join, leave, scan) go on the USER virtual channel. They are
  defined once in `wifi_ctrl_proto.h`, shared by both sides.
- **BL616 task rules (rev 1 bring-up, tests/test100):** the Wi-Fi manager's event callback never blocks (it runs
  with the scheduler suspended) and the NetHub virtual-channel task never blocks (the K230's credits stop): both only
  queue, one worker (`wl_ev`) does the work and the sends; sends retry for up to 2 s (5 upload buffers); one scan at
  a time; a join leaves first.
- **The card interrupt (tests/test100, 2026-10-25).** RT-Thread's SDIO core enables a function's interrupt in the
  card but never the host's card interrupt, and the K230 SDHCI arms only half of it at init: no BL616 message ever
  woke the worker, everything ran on the 1 s safety poll (link up 2 s late, DHCP 2.8 s, joins 9-14 s).
  `k230_board/install.sh` arms it at the first attach; `bl616_stat` counts interrupts against polls (late polls 0).
- **DHCP without the ARP probe.** lwIP's check of the offered address (500 ms timeouts on its 500 ms DHCP timer) held
  the address back up to 1 s: off on the K230 (install.sh, lwipopts.h). The lease is the BL616's MAC's anyway.
  K230 card bound -> address 0.33-0.59 s with the BL616 already joined (was 5.05 s).
- **Throughput, measured** (test100 step 4, home router ch 11, -47 dBm, LAN PC wired): TCP up **17.1 Mbit/s**
  (12-20 per second), down **~26 Mbit/s** (18-31); after the upload fixes below **27.5 up** (peaks 32), UDP 35.4.
- **Upload limit (2026-10-25):** the bridge, not the radio. The K230 driver now waits up to 20 ms for a transmit slot
  instead of dropping the frame (drops made TCP back off), with 16 slots; the BL616 gets 26 NetHub download buffers
  and 23 netdev credits instead of 15 / 8 (`firmware/bl616_common/sdk_patch.py`, applied by host.mk before every build). Two H.265 streams at full resolution (2592x1944, an estimated
  4-8 Mbit/s with audio) fit; RTMP over TCP stalls on the dips, so a steady upload matters more than the peak. Next:
  find the upload limit (air, BL616 transmit credits, one frame per SDIO transfer, K230 TCP buffers; a UDP run and
  the counters), then the real load (two RTMP streams, after the cameras).

### 5.6 Imaging pipeline per camera

```
capture (LEDs at 100 %, AE settled)
 -> imgdiff vs the history view: a low-threshold gate, its job is recall
 -> change net on the KPU where imgdiff saw a change: light only -> not changed
 -> anything changed, or a sensor alarm -> server check (leak / no leak), history
```

- **Change detection (`imgdiff.c`).**
  - Frames are reduced to 320x240, masked to the image circle and gain-normalised.
  - Blocks are compared on a 16x12 grid.
  - One reference per camera: the history view (`cam<N>.hist`), the last keyframe with every
    stored delta applied. A slow seep is not absorbed, because nothing enters the history
    before the server has judged it.
- **Image history on NAND.** It must be on NAND because the K230 loses its RAM at every
  power-off.
  - Per camera: a keyframe (full luma, compressed) plus deltas holding only the changed 80x80
    blocks.
  - A new keyframe after 96 deltas, or when more than half the image changed.
  - A 4 MB quota per camera (one keyframe group: the history needs only one large image per camera);
    a group over half the quota (2 MB) starts a new keyframe, so the oldest group can go (the prune never
    deletes the current group);
    the oldest group is deleted first.
  - Every file is written as tmp + fsync + rename, with a CRC.
  - Reference files use two slots, because UFFS cannot rename onto an existing file.
- **LED control.** Start at maximum current, because 2 m must be lit. Back off only when
  highlights clip. White and IR are independent; IR is useful only with the IR-cut filter
  removed.
- **Change net** ([firmware/NN.txt](firmware/NN.txt): design and retraining plan; `change.[ch]`).
  - MobileNetV2-0.35 with frozen ImageNet weights up to stride 16: a 15x20x192 feature map of
    the reduced frame, int16 kmodel (372 KB), 69 MMAC.
  - Distance: the max over cells of (1 - cosine) between the history view and the current
    frame. Per cell, because a small puddle changes 1-3 of 300 cells.
  - Below 0.44, the largest distance of any lighting or AGC pair, the change is light only
    and the wake sleeps without Wi-Fi. The history view stays, so a growing leak is still
    compared with the dry floor.
- **Encoders.** H.265 (Main) only, one VENC channel per camera.

### 5.7 Streaming and audio (built, not run)

- **Streaming (`leakcam_stream`).**
  - Push mode (`-p host:port -t seconds`), used by `leakcam_wake` when the server's answer asks
    for a clip (`"video":<seconds>`, on a leak or on request; at most 300 s): both cameras live
    over RTMP to the server (port 1935, `leakcam/cam<N>`), each H.265 with the microphone: video
    is always with audio. The bench watches the same push on a PC (MediaMTX or `ffplay -listen`);
    there is no RTSP server on the board.
  - Set for battery: 500 kbit/s and 15 fps per camera, an IDR every 10 s, so one per short clip (a 5 s clip is about
    0.35 MB per camera). Bytes are Wi-Fi airtime and frames are ISP and encoder work.
  - Hardware end to end: each camera's VICAP/ISP channel is bound to its H.265 encoder
    (`kd_mpi_sys_bind`), so frames never pass through the CPU; it moves only the bitstream.
- **Audio, in every stream.**
  - 16 kHz mono from the codec left input, Opus at 24 kbit/s (bundled libopus, complexity 3, a
    few percent of a core): wideband at a third of G.711's rate, and it plays on phones and in
    browsers. No DTX, so quiet leaks are never dropped as silence. Carried as Enhanced RTMP v2
    next to H.265; the server's ingest is MediaMTX, which takes both (the host test uses FFmpeg
    7.1+).
  - Quiet leaks: mic PGA 30 dB plus ALC analog +24 dB, all before the ADC; clips near 80 dB SPL.
    The gains are set after the first enable, which resets them.
  - MIC_BIAS measured 1.4 V at C96 (mic VDD) and FB4.2 (tests/test012): below the 1.5 V this note once asked for, yet
    the mic records cleanly (-42 dBFS quiet floor, speech to -7 dBFS); the microphone's datasheet minimum is not checked.

## 6. Rev 1 changes

Checked against the schematic on 2026-09-24.

**Done in the schematic:**
- PGOOD → BL616 IO03 and K230_PWR → IO30 (USB plug-in wake).
- AHT20 supply RC filter (R52, C4, C9).
- BL616 IO27 → RBT1 10k → BOOT1 (USB flash mode on command).
- TPS61161 instead of TPS61160 for both LED drivers.
- R66 3.0R and R68 1.5R.
- CT1 not fitted.
- LED polarity: all strip connectors have + on pin 2 (the main-board J1/J2 on pin 1), and the
  silkscreen marks every LED (dot = anode, stripe = cathode). The IR part's EasyEDA numbering
  (pad 1 anode, pad 3 cathode, pad 2 centre) differs from its datasheet but agrees between symbol
  and footprint.

- MIC_BIAS decoupling: CB2 100 nF 0201 behind the U3.A4 ball (0.3 mm), CB1 10 µF 0603 4 mm
  away, both before FB4.
- PR1 pad 6 on 3V3_SLEEP, so a fixture can hold AI_BOOT high.
- Fab outputs regenerated from the PCB (2026-09-24): gerber zip, BOM_ASSEMBLY and
  PickPlace_MIFA (244 placements, 56 part numbers, matched to the schematic and the ODB++ board).

**Optional, not done:** move the mic DC blocks C97/C101 toward U3 (design guide: "Place the DC
blocking capacitors for audio input close to the K230 chip"), so the long run is the mic's
low-impedance side. They sit about 15 mm from U3, at the mic. If moved, route C101's ground end
back to U13's GND pad beside MICPL; 1 µF 0402 fits better than the 0805 (footprint change).

## 7. Open firmware work

- **BL616 HBN RAM (fixed 2026-10-22, tests/test021).** HBN RAM in retention mode corrupted accesses after AON
  register writes: the product would have lost its flags, clock and event log on wakes (aon_init() set RET and then
  read; hbn_sleep() set RET and then wrote the event log). Fixed in aon_state.c / leak_wake.c, state moved into the
  linker section .hbn_noinit; verified in the test firmware, product build pending (rewrite).
- **BL616 RTC clock (fixed 2026-10-22, tests/test031).** Two causes stopped the Y3 RTC in HBN: the HBN pin pulls
  on GPIO16-19 (IO16/17 = Y3), which pm_hbn_mode_enter() leaves on, and Y3 selected before it runs. Fixed in
  leak_wake.c (HBN_Hw_Pu_Pd_Cfg(DISABLE) in hbn_sleep(); rtc_use_crystal() waits for Y3, else RC32K); verified in
  the test firmware, 5 of 5 wakes. Open: Y3 -110 ppm (load caps).
- **Full sensor resolution (decided 2026-10-21, refactor later).** The cameras are to run at the OV5647's native
  2592x1944 instead of the 1280x960 binned mode the code uses now (WIDTH/HEIGHT in leakcam_wake.c and
  leakcam_stream.c; the trimmed /bin keeps the 1280x960 ISP set until then). What it touches:
  - ISP set 2592x1944 in /bin (install.sh pattern); sensor mode ~15 fps max at full resolution (2-lane MIPI).
  - DDR: NV12 frame 7.6 MB; 2 cameras x 3 VICAP buffers + encoder references ~60-75 MB of 128 MB: size the pools.
  - History keyframe ~3-5 MB per camera (quota 4 MB too small, >= 6 MB); references 2 x 2 x ~5 MB.
  - /sdcard ~40 MB of data -> 64 MiB; UFFS mount time follows used pages: ~+0.8 s per wake (tests/test022).
  - imgdiff grid and mask constants (80x80 blocks, 16x12), change-net input scaling, encoder bitrate.
  - Option noted: capture and clips at full resolution, history/references stored at 1280x960 (boot stays ~0.64 s).
- **The real server.** Replace the mock; same two requests. Add authentication and TLS.
- **Run on hardware.** The agent, `leakcam_wake` and the push mode are built and tested on the
  host only.
- **OTA writer** (5.3.7: from the server, automatic, checksum verified). K230: download into the inactive slot and
  write `ota_meta`; the SPL side already exists. BL616 full firmware: its writer and switch, not designed yet.
- **LEAKCAM dataset.**
  - Fisheye captures from both cameras, white and IR, at several LED levels.
  - Re-measure every threshold; retrain the change net as in [firmware/NN.txt](firmware/NN.txt).

## 8. Check first on the board

- **Safe states.** K230_PWR and K230_RSTN must stay safe while the BL616 resets and is flashed.
- **Rail decay.** Measure 1V8/3V3 decay after power-off, then set the minimum off time.
- **NAND boot.** The boot ROM must read the NAND on a 3.3 V bank, and DDR training must pass.
  `k230_flash` must find the W25N02KV.
- **USB flash mode.** BOOT1 = 1 must drop the ROM into USB mode.
- **Cameras.** Both OV5647 must answer on i2c0/i2c4 without MCLK.
- **Wi-Fi.** SDIO enumeration (function 1 CIS, block size), the card interrupt on DAT1, DHCP
  through the bridge.
- **Microphone.** The MIC_BIAS voltage at C96.
- **LEDs and exposure.** LED brightness at 2 m. The interaction between auto-exposure and the
  LED back-off loop.
- **Current.** HBN current with both comparators and HBN RAM retention on.
