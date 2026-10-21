# test018: K230 USB-C 1: USB boot and flashing speed

Status: **planned** (2026-10-21).

USB-C 1's data lines are used only for USB boot and flashing (the product needs no USB data). Record what the
flash path achieves, and its total time, as a reference for production programming.

## Steps

1. From the flash logs (flash_nand.sh): per-part write speed and the whole erase + write time.
2. USB speed the ROM/loader enumerate at (high speed or full speed; `lsusb -t`).
3. BL616 flashing over PR1: total time (test firmware 4 s, product 13 s, 2026-10-21) and the baud rate.

## Pass

Figures recorded (K230 writes 3.7-5.5 MB/s seen 2026-10-21); a production flash time per board.


## Boot time

n/a: flashing.

## Battery energy

n/a: flashing runs from USB power.
