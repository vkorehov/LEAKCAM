# LEAKCAM rev 1 board tests

One folder per test, in bring-up order: setup, readings, result, and the obstacles met (problem -> cause -> fix),
with the test's own probe sheets and recordings. Board bugs: [bugs/](../bugs/bugs.txt). Rev 2: [bugs/v2.txt](../bugs/v2.txt).
Test tools (rail checks, probe-sheet renderer, BL616 test firmware, k230_hello): [tools/power-bringup](../tools/power-bringup/README.md).

| Test | Date | Result |
|---|---|---|
| [test001 power and charging](test001-power-and-charging/README.md) | 2026-10-17 | battery rails pass; **charging fail** (bug-001) |
| [test002 BL616 flash and console](test002-bl616-flash-and-console/README.md) | 2026-10-17 | pass |
| [test003 K230 rails and USB boot](test003-k230-rails-and-usb-boot/README.md) | 2026-10-17 | pass; USB-C attach with rails on: bug-002 |
| [test004 K230 hello from SRAM](test004-k230-hello-from-sram/README.md) | 2026-10-17 | pass |
| [test005 K230 NAND flashing](test005-k230-nand-flashing/README.md) | 2026-10-17 | pass |
| [test006 K230 NAND boot (SPL)](test006-k230-nand-boot-spl/README.md) | 2026-10-17 | pass |
| [test007 RT-Smart boot](test007-rtsmart-boot/README.md) | 2026-10-18 | pass; PMU worked around (bug-003) |
| [test008 LED chains](test008-led-chains/README.md) | 2026-10-16/18 | pass |
| [test009 K230 <-> BL616 UART link](test009-uart-link/README.md) | 2026-10-18 | pass |
| [test010 NAND persistence, full disk, speed](test010-nand-fill-speed/README.md) | 2026-10-18 | pass |
| [test011 microphone](test011-microphone/README.md) | 2026-10-18 | pass |
| [test012 KPU change net](test012-kpu-change-net/README.md) | 2026-10-18 | **open**: KPU works, our kmodel gives NaN |

## Status by subsystem (2026-10-18)

| Subsystem | State | Test |
|---|---|---|
| Battery rails, 3V3_SLEEP | pass | test001 |
| Charging | **blocked**: bug-001, 47 k VUSB-TS to fit; cell at 2.9 V | test001 |
| USB-C power with K230 on | **open**: bug-002, plug USB before `pt_on` | test003 |
| BL616 flash, USB console, AHT20, leak ADC (dry) | pass | test002 |
| K230 rails, boot ROM, debug UART, code from SRAM | pass | test003, test004 |
| NAND flash + boot (SPL, DDR 2667, OpenSBI, RT-Smart), UFFS | pass | test005-007 |
| K230 PMU / RTC | worked around: bug-003, no PMU, soft RTC | test007 |
| White + IR LED chains | pass | test008 |
| K230 <-> BL616 UART link | pass | test009 |
| NAND persistence, full disk, speed | pass: read 9.4 MB/s (shell) / 12.7 MB/s (user program), write 1.8 MB/s | test010 |
| Microphone | pass: -7 dBFS claps over a -42 dBFS floor at 54 dB | test011 |
| KPU (change net) | **open** | test012 |
| Cameras | not tested: not delivered | |
| Leak probes wet, Wi-Fi through the BL616 | not tested: with the product BL616 firmware | |

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: `sudo setfacl -m u:$USER:rw /dev/ttyACM0 /dev/ttyUSB0` after each re-enumeration.
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
- Audio played inside the VM stutters: judge recordings by their data or play them on the Mac.
