# test006: K230 NAND boot (SPL), 2026-10-17

Boot straps: [boot_straps_board.png](boot_straps_board.png) (where), [boot_straps.png](boot_straps.png) (zoom).

| Step | Result | |
|---|---|---|
| Full image, reset (BOOT1 low) | ROM: `boot failed with exit code 13` (file error in the boot medium) | **fail** |
| BOOT1 read 0.5 V with IO27 parked; driven low (`pt_boot1`), ~0 V | still 13 | not the cause |
| Plain SPL (`fn_u-boot-spl.bin`) at offset 0 | `U-Boot SPL 2022.10`, DDR training `Firmware run has completed 2667`, `Winbond SPI NAND was found`, `OpenSBI v0.9` | pass |

**Pass:** SPL boots from NAND, DDR trains at 2667. The kernel after OpenSBI: test007.



## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: ROM -> SPL banner and SPL DDR training time (now part of test021).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test021 (boot energy).

## Obstacles

- **"boot failed with exit code 13"** = file error in the boot medium (19 = no medium) -> the ROM reads the NAND.
- **BOOT1 at 0.5 V** -> BL616 IO27 floating -> `pt_boot1` drives it low by default (not the cause; bugs/v2.txt 7).
- **Real cause** -> the EVB layout writes the byte-swapped `swap_fn_u-boot-spl.bin`; this ROM wants plain
  `fn_u-boot-spl.bin` -> genimage-spinand.cfg changed.
