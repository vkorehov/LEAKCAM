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
| Capture, history, streaming, audio on RT-Smart | [firmware/k230_capture/rtsmart/README.md](firmware/k230_capture/rtsmart/README.md) |
| Wi-Fi (BL616 NetHub + K230 SDIO driver) | [firmware/bl616_wifi/README.md](firmware/bl616_wifi/README.md) |
| Neural networks, image quality | [firmware/k230_nn/README.md](firmware/k230_nn/README.md) |
| LED strips | [LED_STRIPS.txt](LED_STRIPS.txt) |
| PCB stack and gerbers | [LAYERS.txt](LAYERS.txt), [FAB_NOTES.txt](FAB_NOTES.txt) |
| Off-board parts (battery, NTC, cameras, cables) | [EXTERNAL_PARTS.md](EXTERNAL_PARTS.md) |

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
 |  - BLE: Wi-Fi credentials only |          |  USB0 on USB-C 1 (flashing), UART0 on CH340X   |
 +--------------------------------+          +------------------------------------------------+
            ^ BQ24072 charger, 1000 mAh LiPo, USB-C power
```

### Why two chips

- **The K230D is the camera computer, the BL616 is the always-on part.** The K230D does the
  imaging, encoding and neural networks but cannot sleep cheaply. The BL616 hibernates at a few
  µA and runs the Wi-Fi and BLE radio.
- **The BL616 decides when the K230 runs.** It wakes on its RTC, on a leak probe (analog
  comparator) or on USB plug-in, powers the K230 up, and cuts it off when the session ends.
- **One BL616 firmware in the product.** The power manager and the Wi-Fi bridge are two folders
  today (`bl616_pwrmgr`, `bl616_wifi`) and become one image. That needs the power manager's
  battery path to run the FreeRTOS scheduler during a K230 session.

## 3. Hardware decisions

| Area | Decision | Why |
|---|---|---|
| View | Two OV5647 Pi-v1.3-format boards with 222° M12 fisheye lenses, back to back (CAM1 J5 top, CAM2 J4 bottom), folded over the board | Spherical coverage is a hard requirement: a leak can be anywhere in the room. Sensors are judged by pixels across the image circle. |
| Camera placement | Camera boards (and LED strips) stay at y ≤ 43 mm | 12 mm from the antenna element, about λ/10 at 2.45 GHz |
| Camera clock | No MCLK from the K230; the modules' own 25 MHz oscillator | As on the Raspberry Pi; K230 MCLK pins stay free |
| Camera pin 18 | IO62/IO63, driven low at boot | It is the red status LED on Pi-style boards; it must not light the lenses |
| Storage | W25N02KV 256 MiB SPI NAND, 3.3 V, on OSPI IO14-IO19 | In stock; the SPL/U-Boot built from source know it. Canaan's older k230_sdk only knows the 1.8 V W25N01GW/W25N02JW. |
| IO voltage | Every K230 IO bank on 3V3; only the fixed IO0/IO1 bank is 1.8 V | Matches the BL616's 3.3 V IO. Consequence: Canaan's prebuilt EVB boot loaders (1.8 V banks) must never be used. |
| Boot straps | BOOT0 = 1 (R64 to 1V8), BOOT1 = 0 (R31 to GND): SPI NAND | NAND boot. BOOT1 = 1 selects MMC0, which is the BL616 and never boots, so the ROM falls into USB flashing mode. |
| Forcing USB flash mode | BL616 IO27 → RBT1 10k → BOOT1; test points B1_P/B1_N as manual fallback | The BL616 can put the K230 into USB flash mode without opening the unit (BOOT1 = 1.65 V, reads high on the 1.8 V pin) |
| USB | USB-C 1 = K230 USB0 (flashing, device mode); USB-C 2 = CH340X console on UART0 | Flashing and console independent. The CH340X runs from the switched 3V3, so it cannot back-power an off K230. |
| Power chain | BQ24072 charger (ISET 0.89 A, ILIM 1.0 A, TMR 6.3 h); TPS62823 0V8 → TPS63802 3V3 and 1V8 → TPS62823 1V1; U7 PG → RSTN | Meets the K230 power-up order (core first, IO after, RSTN 12.5 ms after the last rail). Soft charge chosen on purpose. |
| Power-off | Firmware waits ≥ 5 s before re-powering; all BL616 pins toward the K230 go analog before K230_PWR drops | The TPS63802 has no output discharge; pull-ups on the switched 3V3 would back-feed the dead rail |
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
  `firmware/k230_board/install.sh`. The Linux capture build (`k230_capture/v4l2cap.c`) remains
  only as the first prototype.
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
| 48M | 20M | UFFS `/bin` (ISP tuning files) |
| 72M | 176M | UFFS `/sdcard` (programs, image history) |
| 248M | 8M | bad-block reserve |

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
  - BL616 SDIO pads: DRV_0 instead of the SDK's DRV_1 (`bl616_wifi/wifi_link.c`). Per the BL616
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
   leak comparator, or USB plug-in.
2. It powers the K230 and releases reset. It brings up Wi-Fi in parallel, if credentials are
   stored.
3. RT-Smart boots from NAND. The agent sends `READY` on UART1.
4. The BL616 answers `WAKE,<reason>,<unix s>` and, with a fresh humidity sample,
   `ENV,<rh>,<t>`. The agent sets the RTC from the time in `WAKE`.
5. Capture: both cameras stream with the LEDs on and auto-exposure settles; one frame per camera
   is taken.
6. The image-quality check runs, then change detection, then (if changed) the leak network. See
   5.6.
7. Changed blocks go to the NAND history. The result is reported over Wi-Fi when the link is
   up.
8. The agent sends `SLEEP,<seconds>`, syncs the filesystems and sends `HALTED`. The BL616 cuts
   the power and hibernates.

### 5.2 BL616 power manager ([firmware/README.md](firmware/README.md))

- **Hibernate (HBN) between wakes.** Session flags and the wall clock live in the last 64 bytes
  of HBN RAM, with a magic word and a CRC. `HBN_RSV0` is not usable because
  `pm_hbn_mode_enter()` overwrites it.
- **Wake sources:**
  - leak line on ACOMP1, trip at 0.825 V;
  - USB plug-in via PGOOD on ACOMP0;
  - the RTC.
- **USB mode.** On USB power the BL616 stays awake and advertises BLE for provisioning.
  Unplugging returns it to the battery schedule.
- **Recovery.** A hung K230 gets an RSTN pulse first; a full power cycle comes second, with the
  5 s minimum off time.
- **Console on USB CDC.** It requires the SDK shell, FreeRTOS and CherryUSB CDC-ACM. If any of
  these is missing, Kconfig drops the option silently and the console lands on the K230 link
  pins. Check `CONFIG_BSP_CONSOLE_USB_CDC` in the generated config.

### 5.3 BLE: Wi-Fi credentials only

- **One GATT service with write-only characteristics:** SSID, passphrase and commit.
- **Encrypted link, LE Secure Connections Just Works.** Pairing is accepted only while on USB
  power; plugging in is the proof of physical access.
- **Stored in easyflash** (`wifi_ssid`, `wifi_psk`), and nothing else.
- **No other BLE features:** no shell, OAD or status characteristics. Everything else goes over
  Wi-Fi.

### 5.4 K230-BL616 link and time

- **The UART carries session control.** UART1 at 115200 8N1, frames `$<seq>,<CMD>[,args]*XX`
  with an XOR checksum; invalid lines are dropped. It is available as soon as RT-Smart runs,
  without Wi-Fi.
- **Every command is answered.** `ACK,<seq>` means accepted; `NAK,<seq>` means received but refused,
  for example a `TIME` before 2026 or an `ENV` out of the AHT20 range. The sender keeps one command
  in flight, resends it after 300 ms without an answer, and gives up after 5 sends. A NAK ends the
  command at once. A corrupted frame gets no answer, because its seq can't be trusted, so the
  timeout covers it. Repeats (a lost answer) get the same answer again and are not acted on twice.
  A `READY` restarts both sequence states, unless it repeats the one just accepted. The timeout,
  retry count and result names are defined once in `bl616_pwrmgr/k230_link.h`, which the agent
  includes too.
  - K230 → BL616: `READY`, `SLEEP,<s>`, `HALTED`, `TIME,<unix s>`.
  - BL616 → K230: `WAKE,<cold|leak|rtc>,<unix s>`, `ENV`, `SHUTDOWN`, `ACK`.
- **GPIO2 heartbeat.** The heartbeat is edges, not a level, because IO2 is JTAG_TCK at reset.
- **Time.** The BL616 is the clock of record. Its HBN RTC is a 40-bit counter on the Y3 crystal
  plus a (Unix seconds, count) reference, re-based at every boot. It drifts about 1.7 s/day.
  - The time rides in the `WAKE` reply (`0` = not valid), so the K230 has it in the first frame
    it receives, and sets its PMU RTC from it.
  - When the K230 has NTP time over Wi-Fi, it sends `TIME,<unix s>` back.
  - The clock is lost only with the battery. `WAKE` then carries 0 until NTP time arrives
    again.

### 5.5 Wi-Fi ([firmware/bl616_wifi/README.md](firmware/bl616_wifi/README.md))

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
- **Throughput.** Plan for 10-25 Mbit/s: two H.264 streams fit, high-resolution MJPEG does not.

### 5.6 Imaging pipeline per camera

```
capture (LEDs at 100 %, AE settled)
 -> image quality (imgqual.c): too dark / clipped -> LED step, recapture (max 2)
                               sharpness < 0.35 x ref or contrast < 0.5 x ref -> INOPERATIONAL (lens)
 -> imgdiff vs base and vs last: low-threshold gate, its job is recall
 -> change net (feature maps vs stored reference embedding): no meaningful change -> done
 -> leak net (current vs dry base): P(inoperational) first, then severity; alarm after N wakes
 -> history: changed blocks to NAND, report
```

- **Change detection today (`imgdiff.c`).**
  - Frames are reduced to 320x240, masked to the image circle and gain-normalised.
  - Blocks are compared on a 16x12 grid.
  - Two references are kept: the dry baseline and the last wake.
  - The baseline is compared as well, so a slow seep is not absorbed. It is never refreshed
    while severity is raised.
- **Image history on NAND.** It must be on NAND because the K230 loses its RAM at every
  power-off.
  - Per camera: a keyframe (full luma, compressed) plus deltas holding only the changed 80x80
    blocks.
  - A new keyframe after 96 deltas, or when more than half the image changed.
  - A 32 MB quota per camera; the oldest group is deleted first.
  - Every file is written as tmp + fsync + rename, with a CRC.
  - Reference files use two slots, because UFFS cannot rename onto an existing file.
- **LED control.** Start at maximum current, because 2 m must be lit. Back off only when
  highlights clip. White and IR are independent; IR is useful only with the IR-cut filter
  removed.
- **Neural networks** ([firmware/k230_nn/README.md](firmware/k230_nn/README.md)).
  - **Leak net.** The leakcam U-Net plus a camera-failure branch. It compiles to a 275 KB
    nncase 2.11 kmodel and matches TensorFlow in the K230 simulator. Its weights come from the
    Pi USB camera, so it needs LEAKCAM data and a retrain. Use float32 input.
  - **Change net.** MobileNetV2-0.35 feature maps at stride 16 (20x15 cells), compared per cell
    against a reference embedding stored on NAND. Per-cell rather than global, because a small
    puddle changes 1-3 of 300 cells. Needs int16 quantisation; must be trained.
  - **Focus and LED level.** No network is needed: classic signals (percentiles, clipping,
    Sobel sharpness ratio against the reference).
- **Encoders.** VENC has 4 channels: H.264 for each camera and JPEG for each camera.

### 5.7 Streaming and audio (built, not run)

- **Streaming (`leakcam_stream`).**
  - RTSP H.264 per camera (`rtsp://<ip>:8554/cam0`, `/cam1`), 1500 kbit/s, IDR every 2 s and
    on every new client.
  - HTTP `/snap/N.jpg` and `/mjpeg/N` on port 8080, encoded only while someone watches.
  - One VICAP channel per camera feeds both encoders, without copying frames.
- **Audio (`leakcam_audio`).**
  - 16 kHz mono from the codec left input to WAV, with a level meter.
  - Gains are set after the first enable, which resets them.
  - The MIC_BIAS default voltage is undocumented: measure it at C96 first (the mic needs at
    least 1.5 V).

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

- **K230 agent on RT-Smart.**
  - UART1 link and the time in `WAKE` into the PMU RTC.
  - GPIO2 heartbeat.
  - `READY`/`SLEEP`/`HALTED`.
  - Start through `RTT_AUTO_EXEC_CMD`.
- **OTA writer.** Download into the inactive slot and write `ota_meta`. The SPL side already
  exists.
- **BL616 merge.** One firmware for the power manager and the Wi-Fi bridge, which needs the
  scheduler during battery sessions.
- **LEAKCAM dataset.**
  - Fisheye captures from both cameras, white and IR, at several LED levels.
  - Retrain the leak net (and replace its wall-band exposure reference) and train the change
    net.
  - Re-measure every threshold.

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
