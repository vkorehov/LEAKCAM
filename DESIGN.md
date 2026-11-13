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
| Camera names | In the product (app, notifications, help): the **top camera** and the **bottom camera**, never camera 1 / camera 2 (user 2026-11-13) | Camera 0 = J4 = bottom, camera 1 = J5 = top; the numbers stay in the protocol (`cam` 0 / 1), file names and logs. App: `Cams.kt`; skill leakcam-app-ux 2f |
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
| Humidity | AHT20 on the always-on rail with an RC filter (R52 + C4/C9); polled every 10 min (no interrupt pin); **85 %RH (the server's per device, 5.3.16) or more at any reading = a humidity alarm**; the room's temperature and humidity taken only after the board slept cool for 3 h (5.3.15, user 2026-11-10) | No interrupt pin, so polling; the filter keeps Wi-Fi burst ripple off the sensor. On the board it reads the board's heat while the K230 or the charger runs (42.6 C in a 21 C room on USB, test048): a warm board only lowers the reading, so the alarm cannot trip falsely, and the room values wait for a cool board |
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

> The first product (attic/firmware: `leakcam_agent`, `leakcam_wake`, `SLEEP`/`TIME`/`WIFI` frames, NTP). The rewrite
> keeps its ideas (history compare, sensors count as much as the cameras, Wi-Fi only for news) but its flow is 5.3.8
> (one K230 program: relay + leak job), 5.3.11 (K230 on with USB power) and 5.3.12 (battery: HBN between events, the
> server's intervals, time from X-Time).

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
capture both cameras (our exposure, the room's light or the LEDs), compare the native 2592x1936 luma in place
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
| `POST /v1/poll?id=<device>` (the BL616, see below) | the board's metrics (5.3.10) | `{"stream":0}`, or `{"stream":<seconds>}` when a viewer asked for live video and audio |
| RTMP `rtmp://<host>:1935/leakcam/cam<N>` (publish, to MediaMTX) | 5 s live: the camera's H.265 from an IDR (Enhanced RTMP) plus the microphone (Opus 16 kHz mono, E-RTMP v2); both cameras at once | the RTMP server's `NetStream.Publish.Start` |

**Live view on request, checked by the BL616 alone (decided 2026-10-25).** Since Wi-Fi is fast (tests/test100: the
remembered AP joins in 0.24-0.47 s, DHCP ~0.6 s, a request 70-158 ms), the BL616 asks the server on its own whether
anyone wants to watch, without waking the K230:

```
RTC wake (poll interval) -> radio up -> join the stored network at the remembered AP -> DHCP (the BL616's lwIP)
  -> POST /v1/poll
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
    until the earliest. Replaces today's `hum_wakes_left` slice counter. The AHT20 is read first at every wake,
    before any rail or radio warms the board (5.3.15).
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
- **USB mode.** On USB power the full firmware stays awake (60 s check-ins, 5.3.1) with the K230 on the whole
  time (5.3.11); it has no BLE.
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
| K230 | on the whole time while USB power is in (the cable relay + the leak job, 5.3.11), never on battery | on with USB power (5.3.11); on battery for each leak check (5.3.12) |

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
K230's USB0, so the setup firmware powers the K230 (the one product program, 5.3.8: its relay) whenever USB power is
in (5.3.11): phone (OTG) -> K230 USB0 (CDC ACM) -> link UART -> the setup firmware's provisioning (the same
operations and STATUS as over BLE, the same PIN). No NetHub: the K230 goes off with the cable's pull or the switch to
the full firmware after the credentials are saved (the full firmware then powers it again on USB). The setup firmware gains k230_power.c and the link UART framing
(k230_link.h); its RAM has room (no NetHub). Rev 2 option (bugs/v2.txt): the BL616's own USB on USB-C 1, no K230.
A configured board set up again over the cable: the app sends `PIN` + `SETUP` to the full firmware over the cable
(5.3.4: `ID` ends in `full`), it switches to the setup firmware, and the app goes on there.

**Check-in intervals (full firmware, 5.3.12):** the server gives the check-in interval (`"checkin"`, default 60 min)
and the leak check interval (`"leakcheck"`, default 24 h) in each check-in's answer, per device adjustable at runtime
(~0.036 mAh per check-in). The server judges a board offline after three of its intervals missed. Plugging or
unplugging USB checks in at once. What rides on the check-in waits for it, up to one interval: a phone's wake through
the server (test119) and a remote Wi-Fi change (5.3.2). A leak (a camera changed, a wet probe) and the battery-low
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

**The BLE core wedge is detected and recorded (user, 2026-11-09):** the BL616's BLE core can be left asleep so that
it never advertises again - only the EN pin or a power loss clears it (skill bl616-ble-low-power 5; 2026-11-08 a whole
day of "not found" on the cell, cause still open). The setup firmware watches for both forms and writes them into the
retained event log (evlog, 5.2) so they are read after the next plug: **parked** - asleep, advertising per the host,
but no BLE wake for 30 s (every advertisement wakes the chip; each sleep window's end logs how many came);
**init spin** - the BLE start not done within 5 s. With each it dumps the core's registers (DEEPSLCNTL, 0x28000100
whose bit 31 sticks, PDS_CTL/4/5) into the evlog, also at boot and at each sleep's start and end; `blecore` shows them
live. Every test refuses a wedged board and fails a run that leaves one (tools/test-runner phone.wedge()); tests/test139
plugs and unplugs at every timing the relay box can make: no wedge so far. To add once the trigger is known: the
recovery (none by software today).

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

- **Screens (user, 2026-11-07):**
  - **LEAKCAMs** (the status screen): this phone's boards as the server sees them, no Bluetooth or cable search;
    actions Take photo / Take video, the rare ones (Change Wi-Fi, Delete) in the three dots; the status text always
    says the next check-in, in the phone's time zone; under it the board's pending list (5.3.13).
  - **LEAKCAM setup, for new devices only:** keeps looking over BLE and on the USB cable at once (no Find button):
    a board on the cable is added and picked as soon as it answers (USB first), boards over BLE as they advertise,
    the list adjusted on the fly; the texts offer both ways (plug it in, or bring it near); after Done it goes on for
    the next board. **A board already set up from this phone is never listed** (user 2026-11-07); the rows show the
    serial only and the Wi-Fi fields start blank. The PIN's fetch makes the phone a user of the board; a setup that
    fails takes that back (DELETE, as Delete does), so the board stays new and in Setup for the retry after a wrong
    password (test113, 2026-11-08) - not when the link was lost after the commit (the board may finish by itself).
  - **Change Wi-Fi:** only the network for one of mine, sent through the server for its next check-in (5.3.13);
    no search. It shows the network the board is on (not secret) and the password as dots; the same network sent
    without its password is refused (the app cannot send the stored one back).
  - The usual three-lines menu next to the title switches the screens (LEAKCAMs, Setup); with no board yet only
    Setup. An empty LEAKCAMs list opens the setup at once, so does a LEAKCAM plugged in.
- **Design principle: the action row and the status block (user, 2026-11-07).** Every screen is built the same
  way: content (list, fields), then the **action row** (one-line buttons, the rare actions in the three dots at its
  end), then right under it the **status block** (res/layout/status_block.xml, StatusBlock.kt): a spinner line
  saying what happens now (looking, sending; gone when nothing runs), then the message (what to do, what happened;
  red background for a problem). What changes size (the message, a list that fills while looking, the pending list)
  sits below the action row, so the buttons never move. **The setup screen** (user, 2026-11-08: "device listed still
  under the status, why not below the button"): the found boards right under the button, the status block at the very
  bottom. **Done -> the LEAKCAMs screen** at once, the board picked, "Done. ..." in its status (user,
  2026-11-08). **Notifications off: always said on the LEAKCAMs screen** in red with a "Turn on notifications" link to
  the app's notification settings until they are allowed (user, 2026-11-08: without them a leak reaches no one).
  **One setup at a time, the nearest board** (user, 2026-11-08: "once
  LEAKCAM is found, stop spinner", "take strongest signal"): the list shows one board, the strongest signal among the
  new ones (a board 6 dB stronger replaces it before a send; the cable's board always wins); "Looking..." stops once
  it is found (the scan goes on quietly for its live icons) and comes back if it goes. **Status texts are compact and essential** (user, 2026-11-08: "so huge status text"): one or two short
  sentences, what happened and what to do; no explanations a person does not need right now.
- **A denied permission comes with a way back (user, 2026-11-08):** the status text says what the permission is for and
  ends in an "Open settings" link to the app's page in Android's settings (after "don't allow" Android may not ask
  again); back from the settings the app goes on by itself. Bluetooth (Nearby devices) and the leak alert's
  notifications both; a denied notification request is not repeated.
- **Redraw only on change (user, 2026-11-08):** a view is redrawn only when what it shows changes, a live value
  (signal bars, battery) at most once a second; never per BLE advertisement or timer tick. A screen that never goes
  idle wastes battery and stalled the UI tests 17 s per lookup (test106).
- **Every user-facing step well under 15 s, with a spinner (user, 2026-11-07):** a person closes the app after 15 s of
  waiting. Find, wake, pair, save, the switch to the setup firmware, change Wi-Fi and delete each end well under 15 s
  (target 10 s) and show a spinning icon and a line saying what happens while they run; a test measuring one above
  15 s fails. The board's own periods (check-in, leak check) are not user-facing and keep their values (tests shorten
  them). **Exception: the setup of a board on battery** (user, 2026-11-08): the setup firmware sleeps between its 2 s
  adverts and the phone's first connects may time out (GATT 133, 5 s each), so it may take longer (17-26 s measured,
  test116); the spinner and the step line stay. On USB power the 15 s rule holds.
- **A set-up board's Wi-Fi changes only through the server (user, 2026-11-07):** no wake and no Bluetooth change for
  the person ("too complex for user"); the server's wake route and the board's switch on request are gone (test119
  retired). For Bluetooth the person deletes the board in the app: with no user left its next check-in switches it to
  the setup firmware, which advertises, and the setup screen lists it as a new device (5.3.5).
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
- **A set-up board's Wi-Fi changed over the cable** (2026-11-07): the full firmware answers the cable too (5.3.11)
  but has no provisioning. Its `ID` answer ends in `full` (the setup firmware's in `setup`); the app then sends
  `PIN`, `SETUP` and waits through the switch (the K230's USB device goes and comes back with the setup firmware's
  relay), asks `ID` again and, on `setup`, sends the setup firmware's `PIN`, `SSID`, `PSK`, `COMMIT` as for a new
  board. One press of Send for the user; the switch is the app's, not a screen.
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
- **Deleting a board puts it into the setup firmware (user, 2026-11-07)**, waiting for BLE, when this phone was its
  last user (below): its next check-in is answered `410 Gone`. A board out of Wi-Fi never hears it; it is in the setup
  firmware already (presence lost, test117), and after the delete the setup screen lists it.
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
- **The BL616 full firmware: staged compressed, unpacked by the setup firmware** (user, 2026-11-06). The 4 MB flash has
  no room for a second full image (slot 0 2 MB = full 1.06 MB, slot 1 1.44 MB = setup 1.26 MB), and the old image must
  never be erased before the whole new one is on the board (a Wi-Fi loss mid-download would leave no full firmware: no
  leak detection). So: the full firmware, while it runs on, downloads the SDK build's `.xz` (0.58 MB, LZMA2 dict 32 KiB)
  into the free end of its own slot 0 and checks its SHA-256 by read-back; a failure changes nothing (the next check-in
  retries). Then it switches to the setup firmware with reason `ota`; the setup firmware checks the copy again, unpacks
  it over the old image (no network), checks the result's SHA-256 and switches back. A power cut restarts the unpack
  from the intact copy (`full_broken` blocks every other way back to slot 0 meanwhile). Image + copy = 1.6 MB of 2 MB:
  the full image can grow by ~260 kB (24 %) before an offer stops fitting; the board refuses one that does not fit and
  touches nothing. Server: `bl616_full.bin` (its version from its build tag) + `bl616_full.xz` in LEAKCAM_IMAGES.
- **The K230 image: A/B slots, the product program inside them** (user, 2026-11-06). The SPL already does the A/B
  part: `ota_meta` (256 KiB at 1 MiB) holds one 512-byte record per slot (A in the first half, B in the second:
  magic `OTAV`, version, CRC-32 of the record with its CRC field 0); the higher version boots, A on a tie; it checks
  the slot image's SHA-256 (its K230 firmware header, `none_sec.signature`) and boots the other slot when it fails.
  A slot (`opensbi_rtt_system.bin`, 1.5 MB of 20 MB) held only OpenSBI and the kernel, the program lived on `/sdcard`
  (UFFS, one copy): so **the product program moves into the kernel image's ROMFS** (CONFIG_RT_USING_DFS_ROMFS), and one
  slot = kernel + program, updated and rolled back together. `/sdcard` keeps data only (image history, ISP files,
  the model). The writer: the new image into the inactive slot, its SHA-256 checked by read-back, then that slot's
  `ota_meta` record with version = the active one's + 1, written last: a power cut before it leaves the old slot
  booting; a bad image is the SPL's fallback.
- **The pre-trained model: its own update, per device, the device ID in the API** (user, 2026-11-06; the file swap
  below proposed). The model (kmodel, several MB) changes
  on another schedule than the firmware and **can be optimised for each device** (its scene, its camera), so it is
  not in the image: the server keeps a model per device ID with a common default, and each board's check-in answer
  names its model's version, SHA-256 and size; the board fetches it with its ID in the path,
  `GET /v1/devices/{id}/model` (the server answers that device's model, else the default). On `/sdcard` as a file:
  written as `<name>.new`, its SHA-256 checked, then renamed over the current one with the previous kept as
  `<name>.old`; a model that does not load or fails its known-output check (skill k230-kpu) falls back to `.old` and
  is reported. Independent of the A/B slots: an image update keeps the model, a model update keeps the image.
- **The SPL never read `ota_meta` on NAND** (found 2026-11-06): Canaan's `k230_read_meta_block()` reads it from SD
  only and answers -1 on NAND, so both records were "invalid" and slot A always booted. install.sh patches the SPL to
  read the record's 512 bytes from NAND (its own erase block; a bad one = no record).
- **Built (2026-11-06), not run:** the BL616 part (server offer and download, ota_fetch.c, ota.c with the xz
  decoder); the K230 slot writer `/dev/ota` (drivers/ota_nand: the inactive slot by the SPL's rule, bad blocks skipped
  as the SPL skips them, read-back, the record last; msh `ota_records`, `ota_file <path>` for the bench); the program in
  the slot's ROMFS (`/app/leakcam`, the SDK's build links the kernel again after the program); the server's
  `GET /v1/images/k230_rtt` (ETag = the SHA-256 in its K230 header, 304 when current) and
  `GET /v1/devices/{id}/model` (the device's model or the default, ETag = SHA-256). **Open:** the transport to the
  K230 (its own Wi-Fi through the BL616's NetHub needs the K230's normal session first) and the model swap on the K230.

### 5.3.8 One K230 product program, the same on every power-up (user, 2026-11-05, revised 2026-11-06; tests/test113)

- **One image, one behaviour.** The K230 boots the same product image every time; its product program starts at boot
  (`RTT_AUTO_EXEC_CMD`) and **knows nothing of the BL616's firmwares or why it was powered** (user, 2026-11-06). It says
  READY and takes the WAKE's data (the wall clock, the AHT20 sample, the probe voltage, the last battery: 5.4,
  k230_link.h); the reasons in WAKE are logged, nothing more.
- **Two things run at once, every time:** the **USB relay**, a service that always runs (USB-C 1 CDC ACM <-> the link:
  every line both ways), and the **leak detection** (the job, not built yet), in parallel.
- **The BL616 gives everything its meaning:** the cable's lines (the setup firmware's provisioning, usb_setup.c; the
  full firmware's ID / PIN / SETUP, usb_full.c: the app reaches the board over the cable in either firmware and can
  ask the full firmware for the setup firmware) and the power (5.3.11: on USB the K230 stays on; on battery the BL616
  cuts it once the job is done).
- **The product kernel carries the USB device** (CherryUSB, CDC ACM on USB0); the phone lists it 1.41 s after the K230
  starts (tests/test044).
- **The switch between the firmwares** is a BL616 reboot: the session ends first (counted, 5.3.10), the other
  firmware powers the K230 again (one K230 boot, ~0.9 s).

### 5.3.9 The battery level on every channel to the phone (user, 2026-11-06; tests/test046, test131)

- **Measured by the K230** (the only battery input: VBAT/3 through R59/R61 on ADC_1, switched by the K230's 3V3;
  tests/test023: a mean of 64 conversions, calibrated full scale 5329 mV, within +-10 mV of a meter). The product
  program reads it at its start and **attaches it to every frame it sends** as `;b=<mV>` (k230_link.h), ACKs too.
- **A periodic `STATUS,<uptime s>,<version>,<die x10>,<heap free>,<heap min free>,<pages free B>`** every 60 s, at
  the relay's start and when the BL616 asks (`STATUS`, the unplug): the battery read again and carried as the
  attribute, so a long or quiet session stays current; the BL616 logs it (`k230: STATUS ...`) and keeps it for the
  check-in (5.3.10). No mode field: the K230 runs the same program every time (5.3.8).
- **Kept by the BL616** (bl616_common/battery.c) in HBN RAM (`.hbn_noinit.battery`; the sections are sorted by name
  so both firmwares put it at the same address): survives sleep, resets and the switch between the firmwares; lost
  only with the battery. `battery` on the console prints it.
- **On USB power the ADC reads the charger's output rail (~4.4 V), not the cell** (test023): such a reading is not
  kept; the last one on battery stays, and the board reports "charging" (USB power, PGOOD) instead. The BL616 has no
  CHG input from the BQ24072: charging = on USB power (the charger charges until the cell is full).
- **Reported on every channel** (the last reading on battery in mV, -1 none; USB power 0/1):
  - Wi-Fi: every check-in, `bat=<mV>&chg=<0|1>` in its form (5.3.10); the server keeps it with the check-in, `GET /v1/devices/{id}`
    answers `battery_mv` and `charging`.
  - BLE: the advertisement's manufacturer data, byte 10: the battery in 20 mV steps (0xFF none); USB power is the
    flags byte's bit 1 (as before).
  - USB cable: `ID <id> <stored> <battery mV> <usb power> <firmware>`.
- **The app** (BatteryIcon.kt) shows one battery icon per board, filled to the charge, **green while charging** (on
  USB power), red at 15 % and below, grey otherwise; only the outline while no level is known. The percent is on the
  icon's tooltip (mouse hover, long press): "Battery 73 %", or on USB power "Charging (USB power) · 73 % when last on
  battery". The charge comes from the voltage by a 1-cell Li-ion curve (4.20 V = 100 %, 3.10 V = 0 %: the K230's
  cut-off under load; the reading is under the K230's own ~200 mA load, a little below rest). The freshest channel
  wins: whichever told last (BLE and the cable live, the server as of the last check-in).

### 5.3.10 Every metric in every check-in (user, 2026-11-06; tests/test136)

Every check-in carries **all the metrics the board can give** (user: temperature and humidity over I2C, uptime, the
firmware versions, the probe voltage, "all metrics possible", and every extra the BL616 and the K230 can give). The
check-in is `POST /v1/poll?id=<id>` with a form (the earlier GET's query had no room: a request buffer of 768 B):

| Key | What | Source |
|---|---|---|
| `ssid`, `full`/`setup` | the network, the running image and its version | api.c |
| `bat`, `chg` | the battery (mV, -1 none), USB power | battery.c (5.3.9) |
| `t`, `rh` | the AHT20: 0.1 C, 0.1 %RH (rh -1 = no reading, t then 0) | I2C0 (IO28/IO29), read before the radio starts |
| `troom`, `rhroom`, `roomago`, `quiet` | the room's temperature and humidity (0.1 C, 0.1 %RH), read only after 3 h cool (5.3.15), s since it was taken (-1 = before this boot), s the board has been cool now; the first three left out until a room reading exists | climate.c (full firmware) |
| `probe` | the leak probe node, mV (1650 dry, below the probe threshold wet, 1100 by default; -1 none) | ADC channel 0 (IO20) |
| `thmv`, `thrh` | the leak thresholds in use: the probe's comparator reference (mV), the humidity alarm (%RH) (5.3.16) | thresholds.c |
| `tdie`, `vdd` | the BL616's die (0.1 C, -1000 none) and its supply (mV) | ADC TSEN, VBAT/2 channel |
| `up`, `boots`, `rst`, `crash` | BL616 uptime (s, this firmware), boots since the battery, the reset cause (GLB reset recorder bits, hex), the crash record's count | metrics.c, crash_rec.c |
| `free`, `minfree`, `blocks` | the BL616 heap: free, lowest since boot, live allocations | mem_stat.c |
| `rssi`, `ch`, `scan`, `join`, `dhcp`, `fails` | this check-in's AP (dBm at the scan, channel), the time of each step (ms), failed check-ins before it | wifi_check.c |
| `sw`, `usbw`, `ks`, `kfail`, `kon` | since the last good check-in: firmware switches, USB plug wakes, K230 sessions, failed ones, the K230's rails-on time (ms) | metrics.c |
| `klast`, `kms`, `kago` | the last K230 session: ok 1 / failed 0 / none -1, its length (ms), s since its end | k230_session.c |
| `kver`, `kup`, `ktdie`, `kheap`, `kheapmin`, `kpages`, `ksago` | the K230's last STATUS: program version, uptime (s), die (0.1 C), kernel heap free and lowest free, free pages (B), s since (-1 = before this boot) | STATUS frame (k230_link.h) |

- **Sensors read fresh before every check-in** (metrics_sample(), ~70 ms; the ADC calibrates once per boot), before
  the radio starts: the AHT20's RC filter is there against the Wi-Fi bursts. The Wi-Fi figures are this run's: the
  form is written when the request goes out (wifi_check.c's `form` hook).
- **Kept in HBN RAM** (`.hbn_noinit.metrics`, both firmwares at the same address): boots, counters, the K230's last
  STATUS and session survive resets and the switch between the firmwares; lost only with the battery. The counters
  start again after a check-in the server answered; a failed check-in counts in `fails` and keeps them.
- **The K230's STATUS** (every 60 s, 5.3.9) now also carries the program's version (firmware/VERSION + the commit, as
  the BL616's), the die temperature (`/dev/ts`) and the kernel's memory (`/dev/sysstat`, a small LEAKCAM driver:
  the SDK's canmv_misc device has the same numbers but brings NTP, soft I2C and touch along).
- **Ages on the RTC** (2026-11-10): ages (`kago`, `ksago`, `jago`, `roomago`) are kept against the HBN RTC
  (rtc_clock.c), which runs through HBN sleep, every wake and every reset and stops only with the battery (as HBN RAM):
  ages across resets and firmware switches. The wall clock (the server's `X-Time`, 5.3.12) is kept the same way.
  `job` is the last leak check's DONE (5.3.12). `hbnw`: HBN wakes since the last good check-in (a wake is not a boot:
  `boots` counts resets and power-ons only).
- **Wake counters for wakes not built yet** (RTC, leak) join the form with those wakes; there is no field for
  an event that cannot happen.
- **The server** keeps the last check-in's metrics in `GET /v1/devices/{id}` (`metrics`, by these keys, integers as
  numbers) and appends every check-in to LEAKCAM_METRICS (one JSON line each: device, UTC time, image, version, battery,
  metrics) for the history. Unknown keys are dropped. `metrics` on the BL616 console prints the form as it would go.


### 5.3.11 On USB power the K230 runs all the time (user, 2026-11-06)

**Rule: whenever the setup or the full firmware sees USB power (PGOOD), the K230 is on, the whole time, until USB
power goes.** No battery budget applies on USB, and a running K230 has its program, its link and its battery reading
ready at once.

- **Both firmwares, indefinitely** (user: no time limit while USB power is there). The K230 runs the same program
  in either (5.3.8: the relay always, the leak detection in parallel); the session caps apply on battery only, the
  heartbeat watch stays (a failed session starts again 30 s after its end). A firmware switch on USB power ends the
  session first (counted, 5.3.10) and the other firmware starts it again.
- **The battery at once.** The K230's STATUS every 60 s keeps the server and the phone current (5.3.9). On USB the
  ADC reads the charger's rail, not the cell (test023): not kept, the board reports charging. **At the unplug** the
  K230 is already up: the BL616 asks `STATUS` (no new command: the same frame, asked now), the K230 reads the cell at
  once (no boot first) and its STATUS carries it; the BL616 keeps it, reports it on every channel, then switches the
  K230 off (usb_full.c, within 200 ms of PGOOD falling + the reading), and checks in at once (presence_now): the
  server and the app have the new level within seconds, not at the next battery interval (24 h). The plug checks
  in at once too (charging at the server and the app within seconds, not at the next 60 s check-in).
- **Tests.** test046 (the battery at the unplug, within one STATUS), test047 step 5 (the normal job on USB), test131
  (the app's level right after the unplug).
### 5.3.12 The full firmware on battery: HBN between events (user, 2026-11-07)

- **Asleep in HBN between events** (36 uA, tests/test031), not in the setup firmware (BLE advertising 428-540 uA,
  ~6x the drain). The setup firmware stays for a board with no network, no presence or no user (5.3.1, 5.3.5).
- **Four events wake it:** the **leak check** once a day (RTC), a **presence check-in** every 60 min (RTC; the server
  may set another interval; user: within an hour is quick enough to answer a user's request, a video to record for
  example), a **wet probe** at once (ACOMP1 below 1.10 V, tests/test034), the **humidity reading** every 10 min (RTC,
  ~85 ms awake; 85 %RH or more starts a leak check with the reason `humid`, 5.3.15). USB power (ACOMP0) keeps it
  awake with the K230 on (5.3.11).
- **The leak check** is a K230 session with the WAKE reason `rtc`, `leak` or `humid`: the K230's job (5.3.8) captures both
  cameras and compares each with its history; it reports `DONE,<cam0>,<cam1>` (each `same`, `light`, `changed` or
  `new`). The BL616 keeps the result for the check-in, checks in at once when a camera changed or the probe is wet,
  and on battery cuts the K230's power. A K230 already running (on USB) gets `CHECK,<reason>` instead of a new
  session. Until the cameras arrive the K230 simulates them (a synthetic scene per camera; a scene file on
  /sdcard picks same, light or a new object).
- **The server drives both intervals** (user, 2026-11-07: adjusted at runtime): every check-in's answer carries
  `"checkin":<s>` (5 s - 1 h, default 3600) and `"leakcheck":<s>` (10 s - 7 days, default 86400); per device through
  `PUT /v1/devices/<id>/intervals` (server/README.md), the board takes them with its next check-in and keeps them
  across resets and firmware switches (HBN RAM, metrics.c): a reset with the server out of reach must not fall back to
  the 60 min default (test128, 2026-11-07).
- **The server drives the leak thresholds too** (user, 2026-11-10): the same answer carries `"probe_mv"` and
  `"humid"`; the board keeps them in flash (5.3.16).
- **Time without NTP** (user, 2026-11-07; TLS needs the date): every answer of the server carries `X-Time: <unix s>`,
  and the BL616 sets its wall clock from whatever exchange came last (wifi_check.c); every frame the BL616 sends the
  K230 carries it on (`;t=<unix s>`, k230_link.h), and the K230 sets its clock from it. **Either clock changes only when it
  is more than 5 s off** (user, 2026-11-07), so it is checked on every exchange but not stepped for small drift. The BL616's HBN RTC keeps it through the sleep, resets and firmware switches (wall_clock.c, HBN RAM); after a
  battery loss the first TLS connection trusts the pinned server key (test109), not the certificate's dates.
- **Design decision: the check-in (presence) over mTLS, dates ignored only on the first check without a clock**
  (user, 2026-11-10; SECURITY.txt 4, tests/test109). The chicken-and-egg (TLS checks dates, the date comes from the
  server over that TLS) is broken by the pin, not by dropping TLS:
  - **mTLS both ways:** the board proves itself with its own key and certificate (made at provisioning, kept in
    flash, never printed); the server checks the device certificate's dates with its own clock. Device certificates
    are long-lived (~20 years): the board never renews by its own idea of the date.
  - **The server's key is pinned** in the firmware (the current pin and the next, a rotation delivered by OTA before
    the old key retires). A key that does not match is refused whatever its dates.
  - **No clock (after a battery loss, a first boot): the first check ignores the certificate's dates** (mbedTLS: the
    expired / not-yet-valid flags cleared in the verify callback) and nothing else: the pin, the chain and the host
    name are checked as always. The `X-Time` of that verified answer sets the clock.
  - **With a clock (set from a verified answer, kept on the RTC in HBN RAM): the dates are checked** like any TLS
    client. A clock off by more than the 5 s rule is corrected by the next answer, never by the board.
  - **Time only from a verified answer:** `X-Time` is taken after the handshake succeeded, never from a plain or
    failed exchange; no plain-HTTP fallback.
  - **A floor:** until that first answer the clock reads not earlier than the firmware's build time (logs and crash
    records get a lower bound, marked unverified), never 1970.
  - Fit and cost (BL616 Wi-Fi RAM at 99.6 %, a handshake per hourly check-in: session resumption / a kept ticket) are
    test109 steps 1-2; the K230's TLS (RTMPS, the model) follows the same rule with the BL616's verified time.
  - **Built (2026-11-10):**
    - **PKI:** tools/pki/leakcam_pki.py (openssl): ONE CA (EC P-256, ECDSA-SHA256, 20 years) signs the server's
      certificate (CN/SAN `leakcam-server`, the Pi's IP) and every device's (CN = the device ID, clientAuth), all 20
      years; keys in .secrets/pki (outside git, 600), the CA key never leaves it.
    - **On the board, all in flash:** the CA certificate (`tls_ca`), the board's certificate (`tls_crt`) and key
      (`tls_key`), DER, written once by `make flash` (tools/power-bringup/provision_tls.py: `provision tls` in 48-byte
      hex chunks, length + CRC-32 checked before storing; only lengths/CRCs ever printed). The CA in flash is the
      trust anchor (no CA compiled in): a CA change is a re-provisioning.
    - **The client:** bl616_common/tls_link.c on mbedTLS 3.6 (the SDK's, hardware AES/SHA/P-256): TLS 1.3 with 1.2
      as the fallback, the server's name `leakcam-server` checked whatever the address, dates checked by our own
      verify callback against the wall clock (none while there is no clock), 4 KB records, the whole session on the
      heap and freed after each request. Every request to the API port (8443: the check-in, the crash record, the
      OTA image) goes through it; nothing goes plain to the server, and `X-Time` is taken only from it.
    - **The server:** server/mtls.go, a second listener on :8443 (TLS 1.2+, RequireAndVerifyClientCert with the CA);
      the boards' routes live only there, and a device certificate speaks only for its own ID (CN = `id`, else 403).
      The app and the K230 stay on the plain :8090 until their TLS (open: the phone's TLS to the same CA, the K230's
      uploads/model).
    - **Transport:** HTTP/1.1 over TLS 1.3 / TCP, one request per wake. **QUIC was tried and dropped (user,
      2026-11-10; all QUIC code removed 2026-11-12).** Measured on the board, same firmware and server, 10 rounds each:
      QUIC (picoquic + picotls on the SDK's hardware-backed mbedTLS) handshake 399/447/562 ms (min/avg/max), the request
      433-596 ms, session heap 28-33 KB; TLS 1.3 handshake 307/310/327 ms, ~330 ms with the request, 15.9 KB. QUIC also
      needed +223 KB of flash (a repartition for OTA) and has no way through networks that block UDP.
- **Budget (estimate):** sleep 0.86 + 24 check-ins x 0.036 + one leak check ~0.3 + self-discharge ~0.8 = ~2.85 mAh a
  day (the 144 humidity readings add ~0.02): ~0.9 years on one cell, ~1.8 on two.
- Build order: the K230 job with simulated cameras + `DONE`/`CHECK` (on USB first); the probe and RTC triggers and the
  power cut after `DONE` in the full firmware; then HBN sleep between the events (built 2026-11-10, hbn_sleep.c,
  tests/test141).
- **The sleep (hbn_sleep.c):** on battery, once nothing runs (no K230, no leak check, no check-in running or asked)
  and the next event is 10 s or more away, HBN level 0 until the earliest of the check-in, the leak check and the
  humidity reading (RTC), the probe's next edge (ACOMP1: wet while dry, dry while wet) or the USB plug (ACOMP0). Every
  wake is a reset: the dues, the probe's state and the missed check-ins are kept in HBN RAM against the RTC; a reset
  (not a wake) starts them again (a check-in 5 s after the start, the leak check at once on battery). The Y3 crystal
  clocks the RTC from the first sleep on (rtc_clock_xtal). The BL616's USB console sleeps with it: the bench keeps it
  awake with `hbn off` (until the next reset; the test runner sends it before every unplug, test141 tests the sleep).
  HBN RAM: the LEAKCAM 1 KB is full (the event log 48 -> 44 entries made the room); the SDK's fixed blocks lie below.

### 5.3.13 Change Wi-Fi through the server, the 5-min wake mode, the leak alert (user, 2026-11-07)

Replaces the wake of a set-up board through the server for a Wi-Fi change (it waited for the next check-in while the
person watched: 5.3.4's 15 s rule).

- **Change Wi-Fi:** the person types the new network in the app at once; the app sends it to the server as the
  board's pending change and is done (no waiting). At the board's next check-in the server hands it over in the
  answer; the board tests it with wifi_check (join, address, internet, the API) and stores it only when all pass,
  else keeps the old one. The server forgets the passphrase once delivered. Plain HTTP on the bench, HTTPS (test109)
  before real use.
- **A failed change:** the board tells the server (on the old network) and the phone is notified. The board leaves the
  HBN cycle for the **5-min wake mode**.
- **A leak** (a wet probe, 85 %RH or more (5.3.15), or a camera change confirmed by the leak job): the phone gets the **leak alert** at once
  and the board goes into the wake mode **checking in every 30 s** (user 2026-11-13: "board sleeps only 30 seconds, I
  think it is best user experience"; was every minute since 2026-11-07), waiting for the person's requests (live
  video, photo, video, Wi-Fi): any of them starts within half a minute. Without a response it slows down (user: "if no
  response in 1 day then start sleeping one minute then 5 minutes in one more day, i.e. exponential"):

  | Since the leak opened, or the person last asked the board for something | Check-in | Battery, about |
  |---|---|---|
  | the first day | every 30 s | 120 mAh a day |
  | the second day | every minute | 60 mAh a day |
  | the third day | every 5 min | 13 mAh a day |
  | after it | the board's normal check-in | - |

  A request from the app (live video, photo, video, focus) counts as a response: 30 s again from then. At once back to
  the normal pace on Not a leak / all clear. All server driven (the check-in answer's `checkin` and `mode`;
  server/alert.go `LeakSteps`, TestLeakCheckinBackoff). The battery figures scale the 60 mAh a day measured for one
  check-in a minute; not measured at 30 s.
  A cleared leak is not opened again by the same leak check's result (it rides on the check-ins for 60 s): only a
  check that ran after the clear counts (server/alert.go `clearedAt`, test140 2026-11-12). Open: the board does not yet
  act on `rearm` (a new reference picture after Not a leak).
- **Every leak is recorded: 2 pictures per camera, spaced in time, and a 15 s video per camera** (user, 2026-11-07:
  "4 pictures and one 15 second video"; 2026-11-12: "two separate pictures from both cameras and two separate videos",
  "take 2 pictures per camera, spaced by time so user can see flowing water"). Each camera's two pictures are
  `gap_ms` apart (2000 ms, the server's LeakGapMs): water that flows or spreads differs between them. Every picture
  and video is a file of its own, one camera each (no side-by-side). The board starts them by itself right after the
  leak check that found it (the K230 is on already), uploads them to the media server and reports them; the alert
  opens them in the app. The server also asks for the set (`capture` kind `leak`: 4 photos, `gap_ms`, 15 s video,
  `cams` 2) in its answer to the check-in that reported the leak, for a board that did not start them. Built: the
  server's part; the board's comes with the real cameras (2026-11-12).
- **After the alert, the app shows the leak** (user, 2026-11-07: "after notification, open app and point to those
  pics"): "I see it" stops the alarm and opens the app on that board with its leak panel on top: which LEAKCAM, when,
  what found it, and its record (2 pictures and a video per camera) to look at and play as the board uploads it; until then
  a spinner says it is being taken.
- **Every picture and video has its ID** (user, 2026-11-07): `<device>-<unix s>-p<n>` / `-v<n>`, made by the server
  for each capture. One file per camera (2026-11-12): picture n is camera (n-1) % 2 (p1 and p3 J4 / CSI0, p2 and p4
  J5 / CSI2), video n is camera n-1 (v1 J4, v2 J5); a photo is p1 p2, a video v1 v2, a leak p1..p4 v1 v2. The check-in answer's `capture` gives the board the IDs to upload under; the push (`media`, the
  IDs) tells the app how many new pictures and videos there are.
- **Stored in the phone's gallery, not an own viewer** (user, 2026-11-07): the app downloads each ID
  (`GET /v1/media/<id>`, 404 until uploaded, asked again every 5 s only while the leak panel is open) into the
  gallery's LEAKCAM album (Pictures/LEAKCAM, Movies/LEAKCAM, saved once: the ID kept in the entry's description).
  **The moment it was taken, readable and in the metadata** (user, 2026-11-12: "timestamp in picture naming, also in
  metadata", "user readable, not unix"): the board sends its capture time with each upload (`X-Taken`, unix ms), writes
  it into the file too (JPEG EXIF DateTimeOriginal, MP4 creation time); the server keeps it as the file's time and
  returns it on download; the app names the file `LEAKCAM_<serial>_<yyyy-MM-dd_HH-mm-ss>_cam<N>_<shot>.jpg|mp4`
  (the phone's time, as every time the app shows; cam0 = J4, cam1 = J5) and sets the gallery's date taken to it. The leak panel shows
  **one preview** (the first file there, a spinner until then) with **"+N more"**; a tap opens the phone's gallery on
  its newest pictures and videos, this leak's record at the top (no gallery opens one album by a standard intent).
  Built: IDs, push, panel, gallery download, the server's media route; the board's capture and upload with the cameras.
- **The wake mode** (every 30 s during a leak, slower by the day as above; every 5 min after a failed Wi-Fi change): BLE sleep (the setup firmware's low-power advertising, ~0.5 mA) instead of HBN, a check-in
  at the mode's interval (a request from the server arrives within it). It ends on success / the
  person's all-clear, or after a time limit, whichever comes first (~13 mAh a day: the limit protects the battery).
- **To the phone: Firebase push** (FCM, high priority) from our server, at once, with the app closed too; one path for
  the leak alert and the failed change.
- **The leak alert is the app's most important use case.** Android's government alerts (cell broadcast) are not open
  to apps, so the app makes the closest: a full-screen alert over the lock screen (like an alarm clock or an incoming
  call), an alarm sound repeating until acknowledged, vibration, through silent mode and Do Not Disturb. The app asks
  for full-screen notifications and Do Not Disturb access once at setup. Android fixes a channel's DND bypass when the
  channel is first made, also against a later setBypassDnd(true) and a delete + remake: the alert channel's ID carries
  the access (`leak_alarm` without it, `leak_alarm_dnd` made once it is granted), so access given after the first alert
  still gets through DND (found 2026-11-09: access granted later, the channel stayed mBypassDnd=false). Acknowledging stops the sound and offers the
  requests (all clear, record a video). An SMS backup through the SMS gateway comes later.

### 5.3.14 Take photo, take video: the app's common actions (user, 2026-11-07)

- The status screen's action row for one of the person's boards: **Take photo** and **Take video**, the most common
  actions; Change Wi-Fi and Delete are rare, behind the three dots. A new board is set up on the setup screen (the
  three-lines menu, 5.3.4).
- **When it runs:** at once over BLE when the board is in range (awake), else at its next check-in (the server keeps
  the request and hands it over in the check-in answer). The app never waits for it: the request is sent, the screen
  says when it will happen, and the person gets a notification when it is ready (5.3.4's 15 s rule).
- **A video is 15 s, one per camera** (two separate videos, user 2026-11-12). The board captures them, uploads them
  to the media server (MediaMTX, memory leakcam-server-mediamtx) and reports them; **a photo is one picture per
  camera** (two separate files), uploaded the same way (`POST /v1/media/<id>`, JPEG).
- **Played in the app** once taken: the notification opens it; the board's row lists its latest photo and video.
- **The pending list (user, 2026-11-07), server driven:** what waits for a board's check-ins, one of each at most:
  one photo, one video, one Wi-Fi change (a new change replaces the waiting one; a photo or video asked again while
  one waits adds nothing, and its button is off). One thing per check-in, in this order: a leak's record, the Wi-Fi
  change, the photo, the video (the board checks in again right after a Wi-Fi change). The server lists it per
  device (`pending`: kind, asked, state waiting / sent) with its next check-in (`next_checkin`, unix s); the status
  screen shows it under the status block, the times in the phone's time zone.
- **The board's path (built 2026-11-12, tests/test143):** the check-in answer's `capture` (kind, IDs, cams, the
  leak's photos and gap_ms, `more` = how many still wait) -> the BL616 sends `SHOOT` to the K230 (a session started
  for it when none runs, k230_link.h) -> the K230 asks `WIFI`, the BL616 starts the NetHub bridge, the K230 binds the
  SDIO card and takes its address -> per round one lit colour frame per camera (the LEDs on until auto exposure is
  steady), a JPEG each with the moment in EXIF (software encoder, quality 85, ~0.3 s), `POST /v1/media/<id>` with
  X-Taken (plain HTTP on the LAN until the K230 has TLS, test109) -> `SENT,<uploaded>,<failed>,<skipped>`. A photo:
  ~1.5 s on the K230, 6-9 s from the check-in. Then the board checks in again at once when `more` > 0.
- **Videos (built 2026-11-12, tests/test147; user: "h265 with native resolution 15 frames", "streaming server to be
  golang, same server", "just add lib"):** per camera 15 s at 2592x1936, H.265 Main on the VPU (h265.c: VENC 1 + cam,
  CBR 2 Mbit/s, an IDR every 15 frames), fed the running native streams (cap_stream: no copy, no restart), the LEDs on
  through the clip when the shot needed them. Each camera's frames go out as they are encoded: a chunked `POST
  /v1/media/<id>-v<n>` (`video/x-leakcam-h265`: [u32 length][u64 the frame's own time, us][access unit]), one uploader
  thread per camera. The LEAKCAM server itself (server/video.go, gohlslib + mediacommon: no MediaMTX) serves it live
  as HLS (`/v1/live/<id>/index.m3u8`, from its 2nd segment) and writes the MP4 at the end (hvc1, the frames' real
  times, the creation time = X-Taken). Measured: both videos uploaded, ffprobe clean, live during the recording.
  - **The rate: 7.5 fps per camera, not 15** (the frames' own times): two native cameras pass the ISP at ~80 MP/s in
    all, the same ceiling test030 met at 1280x960 (32.7 fps each); the ISP clock is not it (PLL0/4, 1/1 = ~400 MHz,
    read from CMU 0x91100068). Open: 15 fps native needs ~151 MP/s from the camera library's two-camera mode.
  - **Memory:** one native H.265 channel takes ~14 MB of RT-Smart pages (list_page): RT-Smart 52 / MMZ 76 MB, the
    leak check's 5 MB history buffer only during a check, the JPEG channel closed for a video.
  - **The link:** 2 x 4 Mbit/s overran the product's Wi-Fi bridge (lwIP out of memory, a 2 MB backlog); 2 x 2 Mbit/s
    goes through (backlog < 0.13 MB). The encoder's first fetch can bring the VPS/SPS/PPS alone: the picture is in the
    next fetch (h265.c), and the server puts them in front of every IDR for the live view.
- **The bridge lives only for the capture:** the session ends after SENT (on USB the K230 comes back at once) and the
  BL616's own check-in waits while a capture runs. Both on one radio and one MAC (two IP stacks, two DHCP leases) lost
  the check-in's frames: 3-8 s handshakes and failed check-ins, 3 in a row would switch to the setup firmware
  (test143, 2026-11-12).
- **Exposure and light (user 2026-11-12: "minimise energy waste", "adapt to the external light"):** the ISP's auto
  exposure is off; each shot first reads one frame with the LEDs off (the room's light at the exposure in use). Enough
  for the target at <= 33 ms x gain 4: the shot in the room's light, no LEDs. Else the LEDs at the camera's last good
  LED exposure (kept in /sdcard/leakcam/exposure for cold starts, with the exposure each sensor was left at: the
  cameras open at it, so a probe after a boot reads the room as a running K230 would, 82 against 83 warm; before, 14
  against 75, user 2026-11-12 "add persistence"), right at once in 3 frames: LEDs ~0.1 s per shot
  (the ISP's AE: 14-40 frames, 0.5-1.7 s). One history per lighting (cam<N>.hist LED, cam<N>.amb.hist room).
  Room-lit comparisons see daylight, lamps and shadows (tests/test144: imgdiff calls 23 of 30 lighting changes a
  change); the plan: a room-lit `changed` is checked by flash / no-flash (two frames at one exposure, LEDs on and off:
  the difference is the scene lit by the LEDs alone, the same day and night, compared with the LED history), then the
  change net's `light` verdict.
- **Native resolution (built 2026-11-12, decided 2026-10-21):** both cameras at 2592x1944 (15 fps), the pictures
  2592x1936 (whole 16-row blocks for the JPEG encoder), JPEG on the VPU (~0.2 s per picture, zero-copy, EXIF spliced
  in), the leak check on the native frame too (user 2026-11-12: "low resolution will not see any leak"; 16x16 blocks,
  ~9 cm at 3 m; per camera 0.54 s history read + 0.17 s compare, the LEDs ~0.27 s); only the light probe uses the
  ISP's second channel at 320x240.
  Memory: RT-Smart 48 MB / media pool 80 MB. The sensor mode cannot change per shot (no stream restart, below): the
  lighting probe and the leak check use the scaled channel of the same native stream.
- **White LEDs only:** the lenses' IR-cut filters block the IR chain (measured: room 67, IR 68, white + IR 96 of 255
  at one exposure); IR off, half the LED power. Back on with IR-passing lenses.
- **The cameras stream while they are open** (vicap_cap.c): the closed VICAP/ISP library cannot restart a stream in a
  running program (stop + start, deinit + init fail; a full close + open per shot leaks kernel threads and heap). A
  shot takes frames from the running streams; a K230 session ends them with its power.
- Built: the app's buttons, the pending list, the server's queue and media store, photos and the leak's pictures on
  the board. Not yet: the videos (SENT counts them skipped), playback in the app beyond the gallery download.

### 5.3.15 Temperature and humidity: the room after a cool window, the 85 %RH alarm (user, 2026-11-10; tests/test140, test041)

Restores the humidity use case dropped on 2026-11-06. The AHT20 stays on the board (no sensor off the board in rev 2):
the board is only warm while something heats it, and the readings are taken when it is not.

- **What heats the board:** the K230 (~1 W during a session, on the whole time on USB power, 5.3.11) and the charger
  U1 (BQ24072, linear, ~1.1 W while charging). On USB power the AHT20 read 42.6 C in a 21 C room (test048,
  2026-11-06). The BL616 alone (HBN between events, a check-in's 1-2 s of radio) does not warm it measurably.
- **The cool window:** the BL616 keeps the time the last heat source ended (the K230's rails off with no USB power;
  HBN RAM with the sleep step). A reading is the **room's temperature and humidity** only when the board has been
  cool for **3 h or more**, and it is taken **first thing at the wake** (the AHT20 before any rail, the K230 or the
  radio). A shorter window gives no room reading: the last one stays, with its age.
- **The humidity alarm needs no window:** a warm board only lowers the relative humidity it reads (warmer air at the
  sensor, the same water in it), so **85 %RH or more at any reading** is a real humid room: a humidity alarm, as
  before 2026-11-06. It is a leak trigger like the probe: a check-in at once (the server opens a leak, `why`
  "humidity high"), and a K230 leak check with the WAKE reason `humid`. No memory of an earlier alarm: every reading
  at or above 85 %RH does it again; the server's open leak keeps the phone from being alerted twice, and Not a leak
  keeps it quiet until the humidity has fallen below 85 %RH (as a probe until it dried).
- **When it reads:** every 10 min (an RTC wake in HBN, 5.3.12) and first at every check-in, before the radio.
- **In every check-in** (5.3.10): `t`/`rh` the reading of this check-in (the board's, warm or not), and the room's:
  `troom`/`rhroom` (0.1 C / 0.1 %RH), `roomago` (s since it was taken, on the RTC), and `quiet` (s the
  board has been cool now). No room reading yet: the three are left out.
- **Bench:** `climate` shows the state; `climate now` reads now (as the 10-min timer); `climate quiet <s>` shortens the
  window until the next reset (kept through HBN sleep) and `climate sim <rh %>` / `climate sim off` replaces the
  humidity read (tests only, hands-free: test140, lost at an HBN wake). Shortest settings in tests (memory
  leakcam-test-speed rules).
- **The window through the sleep:** the last heat (RTC s), the next reading and the window are in HBN RAM (climate.c,
  both firmwares at the same address); a reset or a firmware switch starts the window again (whatever ran before
  warmed the board); the K230's power-off and the USB unplug end the heat.
- **The setup firmware** reads too (every 10 min, PDS keeps it): an alarm with a network stored switches to the full
  firmware (switch_to_full_humid), whose first reading at its start raises it; none stored: nobody to tell, setup
  stays (tests/test140 step 6).
- **85 %RH is the default:** the server sets the humidity threshold per device (5.3.16).

### 5.3.16 The leak thresholds from the server, kept in flash (user, 2026-11-10; tests/test142)

The two leak thresholds are the server's, per device, like the intervals (5.3.12): the probe's comparator reference
and the humidity alarm.

- **Attributes of the presence answer:** every check-in's answer carries `"probe_mv":<mV>` and `"humid":<%RH>`
  next to `"checkin"`/`"leakcheck"`. No separate request, no command channel: the board takes whatever the answer
  says, every check-in (state, not events).
- **Set per device:** `PUT /v1/devices/<id>/thresholds {"probe_mv":<mV>,"humid":<%RH>}` (server/README.md); 0 = the
  default (1100 mV, 85 %RH). Ranges: probe 500-1600 mV in steps of 50 mV (the comparator's reference is
  `vio_sel` x 50 mV; dry, the probe node sits at 1.65 V), humidity 50-99 %RH. The server refuses anything else,
  and the board ignores it too. The server keeps them in a file (thresholds.json), so a restart keeps them.
- **In flash on the board, written only when different:** thresholds.c (bl616_common) holds both as one easyflash
  blob `thr`. A check-in answer whose values equal the stored ones writes nothing (an hourly check-in must not wear the
  flash); a changed one is written once. Until the server ever sent any: the defaults. Flash, not HBN RAM: a power
  loss (the cell out), a reset, an HBN wake and the setup firmware all keep them; the setup firmware never talks to
  the server for them and reads the same flash.
- **Applied at once:** a changed probe threshold re-arms ACOMP1 (leak_check_probe(); the HBN wake uses the same
  comparator); the humidity alarm compares every reading with the current value (climate.c).
- **Reported back:** every check-in carries the values in use (`thmv`, `thrh`, 5.3.10), so the server and the app see
  what the board applies, not only what was asked. The server judges a leak in a check-in (`probe`, `rh`) with the
  device's own thresholds.
- **Bench:** `thresholds` shows the values, where they came from (the server's from flash, or the defaults) and the
  flash writes this boot; `leak` shows the comparator's reference as armed.

### 5.3.17 Camera focus: detected when bad, tuned by hand with the app's guide (user, 2026-11-12; tests/test146)

A camera out of focus makes the leak comparison meaningless: a puddle is a few soft pixels, the change rules
(imgdiff's per-block pixel count, 5.3.12) and the change net see nothing. The M12 lenses are focused by turning them
in their holders; a knock, a loose lock ring or a lens swapped for another (the IR question, 5.3.14) moves the focus.

- **The sharpness score (K230):** per camera, on the native luma, the brightness-normalised gradient energy
  (Tenengrad: sum of squared Sobel gradients / mean^2, so the exposure and the light level do not change it) over five
  areas: the centre and four at half the image-circle radius (a fisheye's field is curved: the best compromise, not
  the centre alone). Cheap (~10 ms on 5 x 256x256 pixels), computed on every LED-lit leak check and every frame of a
  focus session. A number compared only with itself (the same camera, the same scene), never an absolute "sharp".
- **Bad focus in a leak check:** each LED-lit check (the light is the board's own, the same every time) compares the
  score with the camera's **focus reference** (the score saved when the person last tuned it, below):
  below 60 % of it on 3 checks in a row = `unfocused` as that camera's verdict in the check-in's `job=` (5.3.10:
  `job=<cam0>,<cam1>,<foc0>,<foc1>`, the percentages, in every check-in). Room-lit checks do not judge focus (the room's light changes the texture).
  A camera without a reference (a new board) reads -1 as its % (`job=...,-1`): the server pushes "tune camera N" once, the row shows the focus icon.
- **The change net's own output (KPU, DESIGN 5.3.12 / firmware/NN.txt):** beside its change / light verdict a third
  output **`unreliable`**: it cannot tell whether anything changed because the picture is not good enough: out of
  focus, a fogged or smudged lens, the lens covered. Trained with those augmentations (defocus blur of each strength,
  fog, smudges, a hand over the lens) on the same pairs as the change verdict. `unreliable` from the net = the same
  `unfocused` verdict; that camera's `changed` / `same` is not trusted (the server does not raise or clear a camera
  leak on it). The score above works without the net and catches focus alone; the net also catches fog and smudges.
- **What the person is told:** the server, on `unfocused` (once, not every check-in): a normal notification (not the
  leak alarm) "Camera <N> of LEAKCAM on <SSID> is out of focus: leaks may be missed. Tap to tune it." -> opens the
  focus screen for that board and camera. The board's row shows a small "focus" warning icon until tuned.
- **The focus screen ("LEAKCAM Focus Adjustment", FocusActivity) over Wi-Fi through the server** (user 2026-11-13:
  "focus tuning is done on already setup phone", "when out of focus detected ... current state stored on server and
  then you can go to focus adjustment screen on selected device. It should offer it on status screen"; the cable
  version of 2026-11-12 is gone): the status screen offers **Tune focus** for a board whose camera is flagged
  (`unfocused` / `untuned` from its check-ins, kept by the server), and in the board's ⋮ menu always.
  - **Start, under a minute** (user: "faster check-ins"): Tune focus -> `POST /v1/devices/<id>/focus {"cam":n}`; while
    a camera is flagged or a session is asked for, the server gives that board a 60 s check-in interval (as the
    5-min wake mode, 5.3.13). The board's next check-in answers `"capture":{"kind":"focus",...}`: the BL616 starts the
    K230 with the Wi-Fi bridge as for a video (5.3.14). The app shows a spinner with the moment it starts. A board not
    flagged (Tune focus from ⋮) is still on its normal interval: its next check-in brings the session (the app says
    when), the 60 s ones follow. The BL616's own check-ins wait while the session has the radio.
  - **The stream:** the K230 holds both cameras streaming (exposures held, LEDs on) and sends the shown camera's
    native H.265 (2592x1936, 15 fps, 10 Mbit/s, as the videos) and each frame's score in one chunked upload
    `POST /v1/focus/<id>/stream`: records of [u32 BE length | kind][u64 BE us][payload], kind 0 an access unit,
    1 << 30 a score line `FOC,<cam>,<seq>,<score>`. The server relays the records as they come to the app's
    `GET /v1/focus/<id>/live` (no HLS, no segments: the picture ~0.3 s behind the lens, HLS would trail 2-4 s), the
    parameter sets kept for a viewer that joins later (it starts at the next IDR).
  - **Commands:** the app's `POST /v1/focus/<id>/cmd` (`{"cam":n}` switch, `{"done":<score>}` keep the reference,
    `{"stop":true}`); the K230 fetches them with a long poll (`GET /v1/focus/<id>/cmd`, answered at once when one is
    waiting, else after 5 s with `{}`). A switch shows at the next frame: one encoder fed only the shown camera, an IDR
    asked for. Done -> the reference saved on the board (/sdcard/leakcam/focus), the server's flag cleared at the next
    check-in's `foc` above 90 %.
  - **End:** Stop, no viewer for 20 s, or 10 min: the K230 ends the upload, SENT to the BL616, the bridge off.
- **The score, once a second over the whole picture** (user 2026-11-13: "feedback speed of 1 second to user is
  adequate", "score on average whole frame"): the K230 scores a grid of 70 tiles of 256x256 (the whole image circle
  but the frame's very corners; Sobel gradient energy / mean brightness^2), 5 tiles per frame of the shown camera, so
  the whole frame once in 14 frames: `FOC,<cam>,<seq>,<score>` once a second at the cost of one frame's 5 areas. The
  tiles in a fixed order, not random: every second the same tiles, so only the focus moves the number. The leak
  check's focus judgement scores all 70 tiles of its one frame (the same score, so Done's reference fits it; the
  reference file is new, `/sdcard/leakcam/focus_ref`: a reference of the earlier 5-area score is not used). The
  video stays live at 15 fps: the person sees the picture sharpen at once, the arrows follow each second. `CAM,<cam>`
  comes before a camera's first picture (the app gives the pictures after it to that camera).
- **The turn guide (FocusGuide, user: "help user with direction where to rotate lens", "if focus gets worse swap
  direction"):** a hill climb with the person's hand as the motor, on each second's score (no median: it would lag
  seconds; simulated: a hand at 5-30 degrees a second reaches the sharpest point within 15 degrees, 8-25 s):
  - start: "Loosen the lock ring, then turn the lens slowly <way> (looking into the lens)", the first way picked at
    random (user 2026-11-13: "rotation direction randomly initially": neither way is likelier right);
  - sharper by 8 %: "keep turning"; blurrier by 8 % without ever getting sharper: "wrong way, turn clockwise";
  - sharper, then 8 % below the best: "past the sharpest point, turn back, small steps" (after that, any fall means
    passed again; the best is the last pass's, so a noise-high best never makes the end unreachable);
  - back within 3 % of the best after a peak: "Sharpest. Hold it, tighten the lock ring, then tap Done" (green);
  - a jump of 30 % between updates: "Turn slower"; no rise or fall for ~10 s: "Nothing sharp in view".
  - Felt as well as seen: a haptic tick at each new best, a long buzz at each "turn the other way".
  - Layout (skill leakcam-app-ux): camera 1 / 2 switch, the live preview, a picture of the lens (an M12 fisheye in 3/4
    view, user 2026-11-13: "static picture of lens and rotation direction") with two arrows around it: green the way
    to turn, red the other (ic_lens_cw / ic_lens_ccw: the two images swap when it gets blurrier; ic_lens_hold green
    ring and tick when sharp; ic_lens alone while waiting), two fixed lines of text, a bar (now, with the best as a marker), "Turn slower" (kept in place, invisible), Done (only when sharp).
    Done on camera 1 goes on to camera 2. Target: one camera in under a minute.
- **A dirty lens** (user 2026-11-13: "offer to wipe camera with alcohol on screen"): the screen always shows, small and
  grey under the guide, "Hazy or streaky at every position? Wipe the lens with alcohol on a soft cloth, then try
  again." (2026-11-13: the bench's J5 camera gave a hazy picture with flare that no software fault explained.)
- **The camera choice is two radio buttons** (Bottom camera / Top camera), as the device list's (user 2026-11-13:
  "change those buttons to be radio buttons ... same approach as device selection. same UX").
- **No automatic camera switch** (tried and removed 2026-11-13, user: "cameras are switching, this is useless, dont do
  this and remove from design"): the screen switched to the hidden camera when its score moved 20 %. With hands at the
  board and the room's light changing that score moved several-fold and the screen jumped between the cameras four
  times in 30 s. The camera shown is the one the person picked, always.
- **No notifications meanwhile** (user 2026-11-13: "i dont want to be notified while tuning focus, nor ... viewing live
  video"): while a focus adjustment or live video is asked for, running, or over for less than 10 min (`FocusQuietS`),
  a camera change is no leak (no alarm, no leak pictures holding the cameras) and no camera problem is told
  (server/alert.go; TestNoCameraLeakDuringFocus); the app holds back camera pushes already on their way while that
  screen is open (PushService). Water at the probe and the humidity alarm still ring.
- **The screen recovers by itself** (5.3.20): an ended or failed session, a camera that did not start, the server
  away: asked for again 3 s later; a live picture without a record for 8 s: the stream reconnected.
- **After tuning:** the histories of that camera are no longer comparable (every block changed): the K230 deletes
  them when it saves the reference, so the next check reads `new` (never a leak) and stores fresh ones.
- **Built (2026-11-12, not yet on the board):** k230_app/focus.c (score, reference, session; FOCUS lines only after
  the phone's PIN got the BL616's OK: the preview is the camera's picture), DONE / `job=` as
  `<cam0>,<cam1>,<foc0>,<foc1>` (the BL616's job field 32 B: the % capped at 999), server/alert.go focusAlerts
  (one `focus` push per camera and reason, `unfocused` / `untuned`), the app's notification, row icon and PIN step.
- **Open:** the lens must be reachable in the enclosure (today the cover has to come off: the fit, memory
  leakcam-fit-rework-pending); the score's 60 % and the guide's 8 % / 3 % are first values, to be measured
  (tests/test146).

### 5.3.18 A camera that sees nothing: an unreliable capture, ignored or tuned (user, 2026-11-13)

User: "dark frame should also be triggering unreliable capture and give user option either to ignore camera completely
or tune its focus in focus screen."

- **Why:** 2026-11-13 the bench's camera 1 looked into a dark room past the LEDs' reach: its kept frame's mean
  brightness was 7 of 255 with the exposure at its end. Sensor noise against the stored history read "changed" in
  17929 of 19602 blocks at every check: a leak alarm, 4 pictures and a video per check.
- **The K230 (job.c):** a kept frame below `JOB_DARK_MEAN` (16; the exposure aims at 90, black is below 4) reads
  `dark`: no comparison, the history kept, the focus not judged. `dark` in the job field
  (`<cam0>,<cam1>,<foc0>,<foc1>`) is not `changed`, so the BL616 and the server open no leak.
- **The server (alert.go focusAlerts):** `dark` is told once per camera like `unfocused` / `untuned` (a `focus` push,
  `why` = `dark`), the camera flagged (60 s check-ins as for a focus problem) until a check reads it fine again.
- **The person's two answers (the status screen, next to the flag):** Tune focus (5.3.17: far out of focus looks the
  same as dark, and the live picture shows what the camera sees), or Ignore camera:
  `PUT /v1/devices/<id>/cameras/<0|1> {"ignore":true}`. An ignored camera's `changed` opens no leak and its problems
  are not told or flagged; `{"ignore":false}` uses it again (a problem it still has is told again). Kept with the
  thresholds (`thresholds.json`, `ignore` in the device's `thresholds`).
- **On the board (user 2026-11-13: "store ignored camera somewhere and skip it. store in flash of k230"):** the
  server hands the mask of ignored cameras to the board at its next check-in as a capture of kind `ignore` (no
  picture, the mask in `photos`; the BL616 passes it in SHOOT). The K230 keeps it in its flash
  (`/sdcard/leakcam/ignore`, written only when it changes) and no longer judges those cameras: they read `ignored` in
  the job, nothing compared or stored. A camera used again starts with `new` (its old history deleted).
  - **State, not events:** the server compares the job's `ignored` with the person's choice at every check-in whose
    leak check ran after the last hand-over, and hands the mask over again when they differ (the K230 was off, the
    write failed).
  - **Still both cameras:** photos, videos and the focus session show an ignored camera too (so it can be looked at
    and tuned before it is used again).
- **The app:** the status screen's flag row says what is wrong ("Camera N sees nothing: its picture is dark", out of
  focus, not tuned yet) with two buttons side by side, Tune focus and Ignore camera (asked first: "leaks in its view
  will no longer be noticed"); "Use camera N again" in the board's ⋮ menu; the `focus` push says "sees nothing".
- **Measured on the board (2026-11-13):** camera 1 in the dark: `dark` (was `changed` at every check); camera 2
  ignored from the app's API: handed over at the next check-in, the next check `dark,ignored` in 0.4 s (1.2 s
  before), no leak; used again: `dark,new`. Server: TestDarkAndIgnoredCamera.

### 5.3.19 Live video (user, 2026-11-13)

User: "we need a live video screen in app", "should be available after leak detected", "in addition of scheduled work,
if no leak", "then still should be available but user have to wait longer".

- **What:** the board's cameras live on the phone, one at a time, with the camera switch (Top / Bottom): the focus
  session's stream (5.3.17: the shown camera's native H.265 at 15 fps through the server, a switch on a keyframe)
  without the lens guide, the bar and Done. The same screen (FocusActivity, `EXTRA_LIVE`), the same server routes and
  the same capture kind (`focus`) on the board: nothing new there.
- **Where:** the status screen's third action, Live (next to Photo and Video), for any board at any time; and Live
  video on the leak panel next to Not a leak.
- **When it starts:** at the board's next check-in, and the screen says when: within 30 s while a leak is open
  (5.3.13), else at the board's scheduled check-in (up to its interval: the person waits longer). The session ends
  when the screen is left, 20 s without a viewer, or after 10 min.
- **Open:** sound (the stream carries none yet; the recorded videos do); the exposure follows the light only at the
  session's start (set for focus tuning: a steady score); measured only on USB power (the battery cost of a session
  not taken).
- **Tested:** on the bench phone with the real board 2026-11-13 (the waiting state with its time, the bottom camera
  live, the top one after a tap); no automated test yet.

### 5.3.20 Every screen recovers by itself (user, 2026-11-13)

User: "why screen never recovers? this is ridiculous, it should be examined so we are eventually consistent, and self
recovering on all screens."

- **The rule:** a screen shows the state of the board and the server, read again and again, never the memory of an
  event. Whatever went wrong (a request lost, the server restarted, the board busy with something else, the network
  away, a session ended by the other side), the screen comes back to the true state by itself: it retries, re-reads
  and says what it is waiting for. No state needs the person to leave the screen and open it again.
- **What that means for a screen:**
  - every wait names what is waited for and until when (the board's next check-in, its time);
  - every failure text ends in what happens next ("Trying again…"), and it does happen, on a timer;
  - a stream or a long request has a stall limit: nothing for that long, and it is started again;
  - a tap that did not reach the server is not shown as done; the screen re-reads the server and shows what is so;
  - nothing a screen shows may be covered or changed by a notification about the same thing.
- **Found the hard way (2026-11-13):** the focus screen stayed on "the board ended the session", on "camera does not
  answer" and on a black picture until it was left and opened again, while the board was busy with a leak's pictures
  that the person's own hands at the lens had triggered.
- **Done:** the focus / live screen (FocusActivity: the ask-and-read loop, RETRY_MS 3 s, STALL_MS 8 s). **To examine,
  screen by screen:** the status screen (it polls: what if a tap's request fails?), setup over BLE and the cable, the
  leak panel and alert, Change Wi-Fi. Each gets a test that cuts the server or the board mid-way and expects the
  screen back without a hand.

### 5.3.21 The cameras' exposure and white balance: right at the first frame kept (user, 2026-11-13)

User: "tune exposure better, it was much better yesterday", "white balance is heavily off", "one is warmer another is
colder", "if environment changes? we should adapt exposure continuously", "can you persist settings so it find faster
next time?"

- **The rule: every setting a camera finds by itself is kept for the next time**, so the next shot starts right
  instead of searching again: a search costs frames, and every frame of a lit shot costs LED time.
  - **Exposure: kept** (per camera, the LED shots' and the sensor's last: `/sdcard/leakcam/exposure`, since
    2026-11-12).
  - **White balance: kept, built 2026-11-13, not verified yet** (k230_app/wb.c). The SDK has no call for it, so the
    closed library's three setters the AWB uses are wrapped at link time (found by disassembly:
    `CamerIcIspWbSetGains` 4 gains, `CamerIcIspSetCrossTalkCoefficients` the 9 matrix values,
    `CamerIcIspSetCrossTalkOffset` 3 offsets, each per camera handle). Under the LEDs the settled set is learnt per
    camera and kept in `/sdcard/leakcam/wb`; when the LEDs go on for a shot it is set at once (the AWB runs only every
    3rd-4th frame: a 3-frame shot is over before its first round), and the room's set is put back after.
    - Measured on the way: the gains alone fixed J5 in a 136 ms shot (red 0.95-1.06 x blue, 0.38 before) but turned
      J4 warm (1.40): the matrix belongs to the state. The version with the matrix and offsets was built and flashed
      at the day's end; **its photos were not taken** (the run was stopped). Until it is proven, a camera without a
      kept set waits 14 frames once (it is learnt then).
- **A frame kept too early (found 2026-11-13, two faults of the same kind):**
  - **Dark:** with the stored exposure already at its limit, the grab kept the first frame at once, one exposed
    before the LEDs' light: mean 15 where the scene gives 105 (and green: blue clipped at that level). Now it measures
    once more at the limit (vicap_cap.c): 105-111 in 5 checks, 120 ms more LED time when at the limit.
  - **Blue:** the LEDs switched on for a shot, the white balance still the room's (warm lamps): the J5 camera's LED
    shots blue (red 0.38-0.75 x blue), its room-light shots and its streams neutral. Now `CAP_LIGHT_FRAMES` 3 frames
    passed after the light changed before a frame was kept (the step before wb.c, replaced by it): both cameras red
    0.99-1.01 x blue, brightness 88 / 94; LEDs
    on 336 ms per lit shot (135 before). The AWB's damping is off too (install.sh patches the SDK's
    ov5647-2592x1944_auto.json); that alone changed nothing.
- **In a session (focus, live video) the exposure follows the light**: every 5 frames one gentle step (the ratio, at
  most x2 / 1/2) when the shown camera's picture left the brightness band (`cap_follow`). Built 2026-11-13, not
  measured yet (a lamp switched during a session).
- **The two cameras are set separately** (each its own exposure and white balance, on its own view): small differences
  remain (brightness within the band 67-117 around 90).

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
  - K230 → BL616: `READY`, `USB,<line>` (the relay), `STATUS,...` (5.3.9), `DONE,<cam0>,<cam1>` (a leak check
    finished, 5.3.12). (The first product's `SLEEP`, `TIME`, `WIFI` are not in the rewrite.)
  - BL616 → K230: `WAKE,<reasons>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv>`, reasons being
    every cause that applies joined by `+` (`cold`, `leak`, `rtc`, `usb`, `humid`, `setup`; e.g. `rtc+leak`)
    (rh -1 = the AHT20 read failed, probe -1 = no reading, bat -1 = no reading since the power loss),
    once per `READY`, answered `ACK,<seq>`; `USB,<line>`, `STATUS` (send it now), `CHECK,<reason>` (a leak check
    now); `ACK`. Every K230 frame carries `;b=<mV>` (5.3.9), every BL616 frame `;t=<unix s>` once it has the time.
- **K230 side: the product program `/app/leakcam`** (firmware/k230_app, in the slot's ROMFS), started at boot
  (`RTT_AUTO_EXEC_CMD`): READY, the WAKE, then the USB relay and the leak job in parallel (5.3.8). (The first
  product's `leakcam_agent` with its capture hook and `WIFI` is in attic/firmware.) Nothing probes MMC0 at boot.
- **The program reads frames from `/dev/link`** (the kernel's link_mux splits UART1 into the console and the frames,
  firmware/CONSOLE.md 4): `poll()` then `read()`. A driver's own `dfs_file_ops` must be attached after
  `rt_device_register()`, which clears them: before 2026-11-05 they were set first, devfs's generic `poll()` never
  reported data and no K230 program had ever received a frame (the old agent's READY went unanswered as well) -
  tests/test045 proves the path end to end, and `link_stat` shows `/dev/link fops attached`.
- **GPIO2 heartbeat.** Every 500 ms while the agent runs; its stop ends the session. It is
  edges, not a level, because IO2 is JTAG_TCK with a pull-down at reset.
- **Time (5.3.12, no NTP).** The server is the clock of record: every answer carries `X-Time: <unix s>`, the BL616
  sets its clock from it, and every BL616 frame carries it on to the K230 (`;t=`; `WAKE`'s unix field too, 0 = not
  known). Either clock changes only when it is more than 5 s off. The BL616's HBN RTC (a 40-bit counter on the Y3
  crystal, drifting about 1.7 s/day) will keep it through the sleep; until then a reset loses it until the next
  server answer.

### 5.5 Wi-Fi ([attic/firmware/bl616/WIFI.md](attic/firmware/bl616/WIFI.md))

- **Split.** The BL616 is only the radio and the WPA supplicant. The K230's lwIP owns the IP
  address: DHCP, DNS and every socket, with the BL616's station MAC (no NTP: the time comes from the server, 5.3.12).
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
  - Native frames (2592x1936 luma, read in place), masked to the image circle and gain-normalised.
  - Blocks of 16x16 pixels (162x121), each judged by its mean |difference|.
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

**Superseded for the product's videos (2026-11-13):** the videos go as chunked HTTP uploads to the Go server
(5.3.14, firmware/VIDEO.txt), each with the microphone as Opus 24 kbit/s (k230_app/audio.c, the settings below);
measured in tests/test148. The RTMP path below stays the test program's (leakcam_stream).

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
- **Full sensor resolution (decided 2026-10-21, built 2026-11-12: 5.3.14).** The cameras are to run at the OV5647's native
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
