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
10. [NAND fill and speed](10-nand-fill-speed.md)
11. [Microphone](11-microphone.md)

## Status (rev 1, 2026-10-18)

| Subsystem | State | Notes |
|---|---|---|
| Battery rails, 3V3_SLEEP | pass | |
| Charging | **blocked** | bug-001 (BQ24072T): 47 k VUSB-TS to fit; cell drained to 2.9 V on 2026-10-18, D1 now off (charger timer/pre-charge state) |
| USB-C power with K230 on | **open** | bug-002: plug USB before `pt_on` |
| BL616 flash, USB console, AHT20, leak ADC | pass | |
| K230 rails, boot ROM, debug UART | pass | |
| K230 code from SRAM (k230_hello) | pass | |
| NAND flash + boot (SPL, DDR 2667, OpenSBI, RT-Smart) | pass | plain SPL; `flash_nand.sh` erases first |
| UFFS /bin + /sdcard, agent starts | pass | busy-wait patch, UFFS mounts |
| NAND persistence, full disk, speed | pass | read 9.4 MB/s (shell), 12.7 MB/s (user program, after the poll fix), write 1.8 MB/s; byte-exact after power cycle |
| K230 PMU / RTC | worked around | bug-003: no PMU, soft RTC |
| White + IR LED chains | pass | J1 pin 2 joint fixed; `led_set()` order fix |
| Microphone (U13 MSM381, K230 inner codec) | pass | claps/speech -7 dBFS over a -42 dBFS floor at 54 dB; clips on claps (gain to choose); MIC_BIAS 1.4 V; poll/pipe kernel patches, audio cache fix |
| Cameras | not tested | not delivered |
| K230 <-> BL616 UART link | pass | both directions, 115200; `READY not answered` = test firmware sends no ACK |
| Leak probes (BL616 ADC, IO20) | pass (dry node) | wet test and the value in the WAKE frame to the K230: with the product BL616 firmware |
| Wi-Fi through the BL616 | not tested | |

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: `sudo setfacl -m u:$USER:rw /dev/ttyACM0 /dev/ttyUSB0` after each re-enumeration.
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
