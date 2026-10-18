# 2. BL616 flash and console (PR1)

- **USB-C cable not detected** -> USB-C port needs 5.1 k CC-GND -> resistor on the yellow CC wire (6.7 k works).
- **Flash mode ignored** -> BOOT sampled only at reset -> link 4-6 first, then tap EN. Later: `reboot uart`.
- **New firmware not running after flash** -> ROM stays in flash mode -> one EN tap.
- **`BLFlashCommand-arm` won't start** -> 32-bit armhf binary on arm64 -> `make flash` runs the x86 tool in the container.
