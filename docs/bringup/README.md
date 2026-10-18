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

**Always:**
- Build host = VMware VM on a Mac: every re-enumerated USB device needs "connect to Linux" on the Mac.
- Serial ports: `sudo setfacl -m u:$USER:rw /dev/ttyACM0 /dev/ttyUSB0` after each re-enumeration.
- Host is arm64; vendor tools are x86-64/armhf: run them in the build container.
