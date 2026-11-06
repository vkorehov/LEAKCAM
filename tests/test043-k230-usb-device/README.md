# test043: the K230's USB device on USB-C 1 (low level, before USB setup)

Status: **pass** (2026-11-05): the K230 enumerates as CDC ACM on USB-C 1 and echoes 1 MB without a wrong byte.
Enumeration 4.1-4.4 s after the K230's start: its USB stack is up at 0.73 s (msh 0.53 s); the other ~3.3 s is
VMware handing the device from the Mac to the VM (the BL616's own USB takes the same 3.3 s after a reboot). A phone
plugged straight into the board has no such step: measure there (test113 step 7).

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

2026-11-05, run.py, 3/3 pass: enumeration 4.4 s after the K230's start (11.3 s in the first run counted the 5 s power cycle before it); 200/200 round trips of 16 bytes (median 997 us, max
1518 us); 1 MB echoed, 0 wrong, 3317 KB/s. Rules: ttyGS0 is registered with DMA RX/TX only, so it is opened with
`RT_DEVICE_FLAG_DMA_RX | RT_DEVICE_FLAG_DMA_TX` (an INT_RX open is refused); a new USB device (1209:ABD1) must be
allowed once in VMware on the Mac before the VM sees it.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.52 / 0.52 s (median / max of 1; SPL 0.16, OpenSBI 0.27, RT-Smart 0.28, mounted 0.51, msh 0.52); longest step to mounted 0.23 s (2026-11-05, run.py) | target (test022) | longest step: |
| This part's boot share | USB stack up 0.73 s after pt_run (msh 0.53 s); the VM sees it at 4.1 s, ~3.3 s of it VMware (2026-11-05) | - | the USB device's start |
| Test run time | 32 s automated (run.py, 2026-11-05), no setup | - | |
| USB device: enumeration, latency, throughput | enumeration 11.3 s after the boot; 16 B round trip median 997 us, max 1518 us; echo throughput 3317 KB/s both ways (1 MB checked) | - | % of the top |
| Current idle / during (min / avg / peak) | n/a: not metered in this run | floor (test031) | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power | - | |
| Per day | n/a: setup only | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | die 52.4 / 52.4 / 52.4 C; board 40.7 / 40.7 / 40.7 C (AHT20) (2026-11-05) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 166736 / min 162524 / 166736 B, blocks 14 -> 14, deleted 0; K230 heap available 7311808 B steady; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-05) | no leak | |
| Image (sizes) | K230 kdimg 45453312 B, BL616 test firmware 1322048 B (2026-11-05, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, sections, --gc-sections (the test image) | smallest code (rule) | |
| Features over the minimal image | CherryUSB device + CDC ACM (test image only) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

The USB device's start adds to the K230 boot: measured in step 1.

## Battery energy

n/a: on USB power.
