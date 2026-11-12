# test006: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 99 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.


### Automated run 2026-10-31 (run.py, 97 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.


### Automated run 2026-10-25 (run.py, 97 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)

### Automated run 2026-10-25 (run.py, 378 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.

- meter K230 rails off, BL616 test firmware awake on USB: 0.061 A
- meter K230 at msh idle (agent running): 0.220 A

### First run, 2026-10-17

Status: **pass** (2026-10-17).

| Step | Result | |
|---|---|---|
| Full image, reset (BOOT1 low) | ROM: `boot failed with exit code 13` (file error in the boot medium) | **fail** |
| BOOT1 read 0.5 V with IO27 parked; driven low (`pt_boot1`), ~0 V | still 13 | not the cause |
| Plain SPL (`fn_u-boot-spl.bin`) at offset 0 | `U-Boot SPL 2022.10`, DDR training `Firmware run has completed 2667`, `Winbond SPI NAND was found`, `OpenSBI v0.9` | pass |

**Pass:** SPL boots from NAND, DDR trains at 2667. The kernel after OpenSBI: test007.

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

#### Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: ROM -> SPL banner and SPL DDR training time (now part of test022).

#### Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test022 (boot energy).

#### Old section placeholders

- Setup: **MISSING**; the setup is in the introduction above and in the Results.
- Steps: **MISSING**; the steps are the rows of the Results tables.
- Pass: **MISSING**; every Results row carries its pass / fail.

## Obstacles

- **"boot failed with exit code 13"** = file error in the boot medium (19 = no medium) -> the ROM reads the NAND.
- **BOOT1 at 0.5 V** -> BL616 IO27 floating -> `pt_boot1` drives it low by default (not the cause; bugs/v2.txt 7).
- **Real cause** -> the EVB layout writes the byte-swapped `swap_fn_u-boot-spl.bin`; this ROM wants plain
  `fn_u-boot-spl.bin` -> genimage-spinand.cfg changed.
