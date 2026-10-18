# LEAKCAM rev 1 bring-up: obstacles

One file per step: problem -> cause -> fix. Board bugs: [bugs/](../../bugs/bugs.txt). Rev 2: [v2.txt](../../bugs/v2.txt).
Readings: [tools/power-bringup/README.md](../../tools/power-bringup/README.md).

1. [Power and charging](01-power-and-charging.md)
2. [BL616 flash and console](02-bl616-flash-and-console.md)
3. [K230 rails and USB boot](03-k230-rails-and-usb-boot.md)
4. [K230 hello from SRAM](04-k230-hello-from-sram.md)
5. [K230 NAND flashing](05-k230-nand-flashing.md)
6. [K230 NAND boot (SPL)](06-k230-nand-boot-spl.md)
7. [RT-Smart boot](07-k230-rtsmart-boot.md)
8. [LED chains](08-led-chains.md)
9. [K230-BL616 UART link](09-uart-link.md)

## Status (rev 1, 2026-10-18)

| Subsystem | State | Notes |
|---|---|---|
| Battery rails, 3V3_SLEEP | pass | |
| Charging | **blocked** | bug-001 (BQ24072T): 47 k VUSB-TS to fit |
| USB-C power with K230 on | **open** | bug-002: plug USB before `pt_on` |
| BL616 flash, USB console, AHT20, leak ADC | pass | |
| K230 rails, boot ROM, debug UART | pass | |
| K230 code from SRAM (k230_hello) | pass | |
| NAND flash + boot (SPL, DDR 2667, OpenSBI, RT-Smart) | pass | plain SPL; `flash_nand.sh` erases first |
| UFFS /bin + /sdcard, agent starts | pass | busy-wait patch, UFFS mounts |
| K230 PMU / RTC | worked around | bug-003: no PMU, soft RTC |
| White + IR LED chains | pass | J1 pin 2 joint fixed; `led_set()` order fix |
| Cameras | not tested | not delivered |
| K230 <-> BL616 UART link | pass | both directions, 115200; `READY not answered` = test firmware sends no ACK |
| Leak probes from the K230 | not tested | |
| Wi-Fi through the BL616 | not tested | |

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: `sudo setfacl -m u:$USER:rw /dev/ttyACM0 /dev/ttyUSB0` after each re-enumeration.
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
