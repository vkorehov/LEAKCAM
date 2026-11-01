# test002: BL616 flash and console (PR1)

Status: **pass** (2026-10-25): flash over PR1 and the USB CDC console work; `pt_status` reads leak node, AHT20 and USB.

PR1 cable and flash mode: [tools/power-bringup](../../tools/power-bringup/README.md) sections 2-3; test
firmware [bl616_powertest](../../tests/firmware/bl616_powertest).

## Setup

PR1 cable (introduction above); BL616 test firmware `bl616_powertest`, flashed with `make flash` (ISP over the running
console, `bl616_isp.py --boot`); console on `/dev/ttyACM0`. Probes: Metrics. Automated: run.py.

## Needs (build features and dependencies)

BL616: SDK board init, USB CDC console (CherryUSB CDC-ACM, CONFIG_BSP_CONSOLE_USB_CDC), shell, FreeRTOS; boot2; flash tool (ISP).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Flash `bl616_powertest` over PR1 (`make flash`); the console comes up on `/dev/ttyACM0`.
2. `pt_status` (run.py: 8 rounds with memory snapshots): leak node, AHT20, USB.
3. `pt_usb`: plug / unplug USB-C 1.
4. `pt_leak` on the dry node.

## Pass

Flashed and console up; leak node dry ~1650 mV; AHT20 reads; USB-C 1 plug / unplug reported (BQ24072 PGOOD); no
BL616 memory leak over the rounds.

## Result

- Flash over PR1 with `make flash` works, no BOOT link and no EN tap (`--boot`, 2026-10-22); console on
  `/dev/ttyACM0` (2026-10-17 run, 2026-10-25 automated run).
- `pt_status`: leak node dry ~1650 mV, AHT20 reads (53.9 %RH, 25.8 C on 2026-10-17), USB detected; 8 rounds all good
  (2026-10-25, run.py).
- `pt_usb`: USB-C 1 plug / unplug reported via BQ24072 PGOOD (2026-10-17).
- Flash write 204 KB/s, ~100 % of the ROM's UART 2 Mbaud link (2026-10-21).

## Metrics (2026-10-21, ISP flash of bl616_powertest)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP at PR1 pad 6, VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY | n/a | - | no K230 |
| This part's boot share | EN tap -> console on USB ~5 s | - | includes the VMware re-attach |
| Test run time | `make flash` 26 s (ISP 1 s, flasher 3.8 s, the rest the container + waits) | - | setup: no hand action (`--boot` since 2026-10-22) |
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
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

BL616 boot to its first useful step (USB console up, or the battery path's decision) - test021 covers the BL616 wake path.

## Battery energy

n/a: flashing and the console run from USB power.
