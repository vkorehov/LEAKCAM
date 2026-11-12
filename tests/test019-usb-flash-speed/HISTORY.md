# test019: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 62 s): pass

flash_nand.sh (erase 0..248M + write 45498368 B): 29.1 s, ok; 1.56 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-31 (run.py, 57 s): pass

flash_nand.sh (erase 0..248M + write 45445120 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-31 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45445120 B): 24.7 s, ok; 1.84 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-31 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-31 (run.py, 57 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 57 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 25.1 s, ok; 1.81 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 57 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.8 s, ok; 1.83 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.6 s, ok; 1.84 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.5 s, ok; 1.85 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-30 (run.py, 57 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 25.0 s, ok; 1.82 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt


### Automated run 2026-10-25 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.6 s, ok; 1.85 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt

### 2026-10-22, run 2 (single-rate images)

K230 26.9 s (erase 8.0 s; one `LIBUSB_ERROR_TIMEOUT` during the erase, normal, it ended "done"). BL616 `make flash`
09:59:26 -> console ~10:00:10, of which ~35 s waiting for the ROM's "connect to Linux"; flasher 4 s.

Production per board (both chips, no hand action but plugging): ~45 s plus the K230 boot-device wait.

### 2026-10-22, run 1 (kdimg 45.4 MB, 4 Mbaud link image)

| Step | Time / speed |
|---|---|
| `pt_run` -> K230 boot device on USB | 6.1 s (incl. the VMware "connect to Linux") |
| USB link | high speed 480 Mbit/s (bcdUSB 2.00, bulk 512 B), ROM and burn loader |
| ROM -> burn loader (re-enumerates as UBOOT) | inside the 25.7 s; loader chunk out 128 KB, in 32 KB |
| Erase 0 .. 248 MB | 8.0 s (31 MB/s) |
| SPL A/B, U-Boot, env, kernel A/B (0.07-1.47 MB each) | 3.1-3.7 MB/s |
| /bin 8.65 MB, /sdcard 34.6 MB | 5.6, 5.3 MB/s |
| **Whole K230 flash** (flash_nand.sh start -> end) | **25.7 s** |
| BL616 test firmware 121 KB (`make flash`: reboot uart -> new console) | 18 s, of which flasher 4 s; no EN tap (`--boot`) |

### 2026-10-21 (from the Steps and Pass of the old README)

- Steps 3: "BL616 flashing over PR1: total time (test firmware 4 s, product 13 s, 2026-10-21) and the baud rate."
- Pass: "Figures recorded (K230 writes 3.7-5.5 MB/s seen 2026-10-21); a production flash time per board."
- Setup: "(29f1:0230 attaches by itself since 2026-10-24; before, "connect to Linux")".

### Superseded status line

- Status: **pass** (2026-10-22, 2 runs): K230 whole image 25.7 / 26.9 s (erase 8 s, write 3.1-5.6 MB/s, USB 2.0 high speed); BL616 ISP flasher 4 s at 2 Mbaud, no hand action; current during the 15 s write not read.

### Superseded metric values

- Test run time (2026-10-22): 25.7 s K230 + 18 s BL616.
- USB NAND flashing write speed, note (2026-10-22): whole flash 25.7 s.

## Obstacles

- **The BL616 lost the NAND boot commands after the flash** (09:48, open): `pt_off`, `pt_boot1 low`, `pt_on`,
  `pt_run` sent 30 s after flashing found the rails off and BOOT1 still high at 09:50 (62 mA, "K230 down"). (Its
  banner in the next read is not a restart: the test firmware prints it whenever the console is opened.) Resent with
  each answer read back -> booted; run 2 (each command's answer read) went through first time. Always read the
  answers back.
- **Build line `An error occurred: 'CONFIG_FAST_BOOT_DELETE_ORIGIIN_FILE'`** (the SDK's image script reading an option
  the leakcam defconfig does not set): the build ends 0 and the kdimg is complete; harmless.
