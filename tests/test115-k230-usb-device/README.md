# test115: the K230's USB device on USB-C 1 (low level, before USB setup)

Status: **planned, built** (2026-11-01): the test image carries CherryUSB CDC ACM; not run yet (the test image needs
rebuilding and flashing: tests/firmware/test_install.sh, then test019's flash).

USB-C 1, the enclosure's only USB opening, is the K230's USB0. For setup over the phone's cable (test113) the K230 must
appear as a USB device. This low-level test checks that first, with the bench standing in for the phone: USB-C 1 is
already cabled to the bench, so nothing is recabled (user, 2026-11-01; the phone over OTG is a hand step later).

## Setup

- K230 test image with CONFIG_ENABLE_CHERRY_USB_DEVICE, USB0, CDC ACM (tests/firmware/test_install.sh appends them to
  the SDK copy of the RT-Smart defconfig; the product image stays without), bench command `usb_echo`
  (tests/test017-nand-raw-speed/leakcam_bench.c).
- udev: 1209:ABD1 -> /dev/leakcam-k230usb (tools/power-bringup/99-leakcam.rules).
- BL616 test firmware (K230 power and console), USB-C 1 cabled to the bench.

## Needs (build features and dependencies)

CherryUSB device stack on USB0, the CanMV CDC ACM class (ttyGS0); nothing else over the minimal image.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Boot: the USB device enumerates on the bench; time from the boot.
2. Echo latency: 200 round trips of 16 bytes.
3. Echo throughput: 1 MB, every byte checked.
4. Then (test113): Wi-Fi setup over it, the bench as the phone, the board's device ID as the USB serial number.

## Pass

The K230 enumerates as a CDC ACM device on USB-C 1 after every boot and echoes 1 MB without a wrong byte.

## Result

Not run (the image is to be rebuilt and flashed).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | the USB device's start |
| Test run time | **MISSING** | - | |
| USB device: enumeration, latency, throughput | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: not metered in this run | floor (test031) | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power | - | |
| Per day | n/a: setup only | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | **MISSING** | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, sections, --gc-sections (the test image) | smallest code (rule) | |
| Features over the minimal image | CherryUSB device + CDC ACM (test image only) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

The USB device's start adds to the K230 boot: measured in step 1.

## Battery energy

n/a: on USB power.
