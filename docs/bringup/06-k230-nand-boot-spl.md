# 6. K230 NAND boot (SPL)

- **"boot failed with exit code 13"** = file error in boot medium (19 = no medium) -> the ROM reads the NAND.
- **BOOT1 at 0.5 V** -> BL616 IO27 floating -> `pt_boot1` drives it low by default (not the cause).
- **Real cause** -> EVB layout writes byte-swapped `swap_fn_u-boot-spl.bin`; this ROM wants plain
  `fn_u-boot-spl.bin` -> genimage-spinand.cfg changed. Result: SPL + DDR training at 2667.
