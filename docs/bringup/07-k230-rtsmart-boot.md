# 7. RT-Smart boot

- **Silent after "OpenSBI v0.9"** -> OpenSBI is quiet; the kernel hung -> found with temporary markers
  (`rt_hw_console_output`, prints via OpenSBI) in the SDK, reverted after.
- **Hang at pin 64 / RTC** -> PMU never wakes (supply + INT4 on switched 1V8) -> no PMU:
  `BOARD_NOT_SUPPORT_HW_RTC`, soft RTC. [bug-003](../../bugs/bug-003/README.md)
- **Link error** -> pins.py footer cut also removed the camera-LED init -> restored.
- **Fault in AI2D driver** -> Canaan's `ai_2d_log()` drops its arguments -> install.sh turns that log off.
- **/sdcard empty, agent not found** -> SDK never mounts the NAND -> install.sh adds UFFS mounts (nand0 /bin, nand1 /sdcard).
- **/sdcard mount fails ("First page is clean in a non-erased block ?")** -> a block with a sealed-but-not-dirty
  first-page tag; mounted once on a fresh chip, fails after re-flashing over used blocks; a full-size (176M)
  image did not help -> open (OOB layout of flasher vs kernel suspected). `/bin` mounts fine.
- **"Too many pending bad blocks"** -> UFFS cleanup queue (4) overflowed by unclean pages; no bad-block marking -> harmless.
