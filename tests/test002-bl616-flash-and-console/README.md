# test002: BL616 flash and console (PR1), 2026-10-17

Status: **pass** (2026-10-17).

PR1 cable and flash mode: [tools/power-bringup](../../tools/power-bringup/README.md) sections 2-3; test
firmware [bl616_powertest](../../tools/power-bringup/bl616_powertest).

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

BL616: SDK board init, USB CDC console (CherryUSB CDC-ACM, CONFIG_BSP_CONSOLE_USB_CDC), shell, FreeRTOS; boot2; flash tool (ISP).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Flash `bl616_powertest` over PR1 (`make flash`) | flashed, console on `/dev/ttyACM0` | pass |
| `pt_status` | leak node 1649 mV dry; AHT20 53.9 %RH, 25.8 C | pass |
| `pt_usb` | USB-C 1 plug/unplug reported (BQ24072 PGOOD) | pass |
| `pt_leak` (dry node) | ~1650 mV | pass |

**Pass.** Wet leak test and Wi-Fi: with the product BL616 firmware (later).

### Automated run 2026-10-25 (run.py, 50 s): pass

8 rounds of `pt_status`: leak node dry, AHT20 read, USB detected; all good.

## Metrics (2026-10-21, ISP flash of bl616_powertest)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP at PR1 pad 6, VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY | n/a | - | no K230 |
| This part's boot share | EN tap -> console on USB ~5 s | - | includes the VMware re-attach |
| Test run time | `make flash` 26 s (ISP 1 s, flasher 3.8 s, the rest the container + waits) | - | setup: 2 "connect to Linux" clicks + 1 EN tap (none since 2026-10-22: `--boot`) |
| Flash write | 116.6 KB in 571 ms = 204 KB/s | UART 2 Mbaud = 200 KB/s | ~100 % (the ROM link is the limit) |
| Current idle / during (min / avg / peak) | not measured: meter not connected | - | on USB (not battery) |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a | - | flashing only on USB power |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 28.8 / 28.8 / 28.6 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; BL616 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 121 KB (+ boot2 53 KB) (bl616_powertest) | minimal image (rule) | the image this run used |
| Compile flags | BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: BL616 test firmware (SDK -O2, no LTO), not the product firmware; the product BL616's flags are test008 step 6 (2026-10-25) | the faster one stays | |

## Boot time

BL616 boot to its first useful step (USB console up, or the battery path's decision) - test021 covers the BL616 wake path.

## Battery energy

n/a: flashing and the console run from USB power.

## Obstacles

- **USB-C cable not detected** -> a USB-C port needs 5.1 k CC-GND -> resistor on the yellow CC wire (6.7 k works).
- **Flash mode ignored** -> BOOT sampled only at reset -> link 4-6 first, then tap EN. Later: `reboot uart`.
- **New firmware not running after flash** -> the ROM stays in flash mode -> one EN tap.
- **`BLFlashCommand-arm` won't start** -> 32-bit armhf binary on arm64 -> `make flash` runs the x86 tool in the container.
- **Flashing needed BOOT link + EN touch every time** -> the ROM also starts download mode after `reboot uart`
  (SDK bl616dk shell: HBN user-boot 1 + power-on reset) -> `make flash` does it over the running console
  ([bl616_isp.py](../../tools/power-bringup/bl616_isp.py)). **Verified 2026-10-21:** `reboot uart` -> ROM `Bouffalo
  CDC` (349b:6160) on USB (after "connect to Linux"), flash 3.8 s at 2 Mbaud, SHA verified. The ROM stays in download
  mode afterwards: `BLFlashCommand --reset` logs "Reset cpu" but the chip does not restart -> one EN tap starts the
  new image (banner build time changed, `pt_status` normal); the HBN user-boot flag does not bring the ROM back.
  Flashing: no BOOT link, one EN tap (was link + two taps).
- **The EN tap after every flash** (2026-10-22) -> `reboot uart` sets the user-boot field in HBN_RSV2
  (0x2000F108), which survives a reset, so every reset lands in the ROM again; only EN clears the HBN domain (the
  2026-10-21 note above was wrong) -> `bl616_isp.py --boot`: in the ROM session the flasher leaves open, ISP memory
  write 0x50 HBN_RSV2 = 0 (as the vendor's `clear_boot_status()` in bflb-mcu-tool) and ISP reset 0x21; `make flash`
  runs it. Verified 2026-10-22 09:38: `reboot uart` -> new firmware console in 18 s, no hand action but the 2 VMware
  clicks.
- **`--boot` refused once ("b''", 2026-10-22 22:29)** -> the ROM had closed the ISP session: it ends one after ~2 s
  without a command, and the flasher's container took longer than that to exit -> `--boot` hand-shakes again
  (0x55 bytes until "OK") and retries; done by hand once, then built into bl616_isp.py.

- **`reboot uart` with the watchdog running looped the ROM until a 3V3 cut** (2026-10-23) -> the 8 s watchdog survived
  the reset into the ROM and reset it before USB came up; the kept user-boot field sent every reset back -> `isp`
  (firmware/bl616/wdg_guard.c: watchdog off, USB detach 0.5 s, then the ROM); every BL616 firmware has it,
  `bl616_isp.py` sends it. `bl616_isp.py` also took the firmware's second port (the K230 console) for the shell ->
  interface 0 only.
- **`isp` still missed the ROM 5 times of ~20** (2026-10-23/24) -> VMware prompts missed or held; popups off since
  2026-10-24, devices attach by themselves; tools wait 20 s, a miss is now a fault to debug.

None in the run of 2026-10-25.
