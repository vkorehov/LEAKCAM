# 7. RT-Smart boot

- **Silent after "OpenSBI v0.9"** -> OpenSBI is quiet; the kernel hung -> found with temporary markers
  (`rt_hw_console_output`, prints via OpenSBI) in the SDK, reverted after.
- **Hang at pin 64 / RTC** -> PMU never wakes (supply + INT4 on switched 1V8) -> no PMU:
  `BOARD_NOT_SUPPORT_HW_RTC`, soft RTC. [bug-003](../../bugs/bug-003/README.md)
- **Link error** -> pins.py footer cut also removed the camera-LED init -> restored.
- **Fault in AI2D driver** -> Canaan's `ai_2d_log()` drops its arguments -> install.sh turns that log off.
- **/sdcard empty, agent not found** -> SDK never mounts the NAND -> install.sh adds UFFS mounts (nand0 /bin, nand1 /sdcard).
- **/sdcard mount fails, random "bad blocks", "can't find right page"** -> Canaan's `spinand_isbusy()`
  abandons a page read when its confirm status read still shows BUSY (0x01); the read returns -1 without
  data, UFFS takes -1 as "ECC corrected", uses stale buffers, refreshes blocks, fails write verifies and
  marks good blocks bad -> install.sh replaces it (`rtsmart/patches/spinand_isbusy.c`: 20 ms real-time
  limit, two "ready" reads in a row). Found with `nandtest` (1000 clean repeat reads) + a status print
  in the read path. 3 reboots + final flash: zero errors.
- **Leftover blocks after re-flashing** -> the burn loader does not erase what an image does not cover ->
  `firmware/k230_board/flash_nand.sh`: pad check, erase 0..248M, write.
- **OOB layout** -> checked, not a problem: the loader puts the image's tag at OOB 4-12, where the kernel
  reads it (the SDK's U-Boot source says 2-15, but the embedded loader is a newer build).
- **"Too many pending bad blocks"** -> was a side effect of the busy-wait bug; gone.
- **"config command failed (-1)"** -> `config.txt` asks for the SD-card resize (no SD card) -> install.sh
  makes gen_config write `board=leakcam` (ignored); the option stays on (`main.c` does not build without it).
