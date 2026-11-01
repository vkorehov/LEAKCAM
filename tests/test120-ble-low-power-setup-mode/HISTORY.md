# test120: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

- 2026-11-01 17:43:25-17:45:27 UTC, BLE sleep with connectable advertising every 2 s (`ble_start_adv 0 0 0x0c80
  0x0c80`, `wakeup_timer 120000 0`), TX power 13 dBm (factory default), USB console off for the window, watchdog
  paused: **USB-C 1 meter 1290 uA** (awake, same advertising: 61 mA). `lp_sleepstat`: 180 sleeps in 120 s = 60
  advertising wakes + 120 wakes of the watchdog feed task (1 s period, still running while the watchdog is paused).
  SDK benchmark for the same interval: 428 uA at 0 dBm TX. Firmware power breakdown printed before USB was back:
  lost.

## Obstacles

- 2026-11-01, the port of bouffalo_sdk examples/pmu/wl_ble_lp to rev 1 (tests/firmware/bl616_lp):
  - the example does not build for the BL616 as shipped: its defconfig sets `CONFIG_TICKLESS_ACCURATE` alone (it
    depends on `CONFIG_TICKLESS_EXT`: tickless_enter undefined), uses a controller library the SDK does not ship
    (ble1m2s1bredr0: ours, ble1m0s1bredr0, has the controller sleep: btble_controller_sleep/_store/_restore) and
    prints lpfw advertising counters that exist only on the BL618DG (on the BL616 the BLE controller sleeps itself,
    lpfw handles Wi-Fi beacons): fixed in our copy.
  - not for our board: its shell on UART0 (our K230 link) and a JTAG remux of IO0-IO3 on every wake (IO0 holds the
    K230 in reset, IO3 is USB PGOOD); its wake clock gating leaves USB off. Changed: the USB console (usb_console2.c,
    suspended before the sleep, resumed after), the USB clock kept, no remux, the watchdog paused across the sleep.
  - first flash: programmed, but the firmware never reached its USB console (no attach, no reset loop); recovery by
    the BOOT link (BUILD.md 4.3 step 4). Boot breadcrumbs added (HBN RAM 0x20010E00, `pt_lpcrumb` on the test
    firmware).
  - with `CONFIG_LPAPP` the SDK's board_init() runs peripheral_clock_init_lp(), which gates every peripheral clock and
    enables USB only with `CONFIG_BSP_USB` (not set), and leaves the watchdog's clock gated. Fixed (`CONFIG_BSP_USB=y`,
    wdg_guard_start() enables its own clock, lp_exit() re-enables both after a wake), but **not the whole cause**: the
    third build (15:33 UTC) still never reached USB (USB-C 1 meter 45-51 mA on the hung builds). Manual ROM entries so
    far: 15:16, ~15:30 and ~15:45 UTC.
  - the boot breadcrumbs cannot work with this recovery: the manual ROM entry pulls AI_EN low, which resets the whole
    chip and evidently clears HBN RAM (`pt_lpcrumb`: none after a build that writes step 1 before board_init). Only a
    software reset keeps them. Next: bisect from the working bl616_powertest by adding the bl616_lp pieces (a step
    that still enumerates costs no hand step).
  - also in the example: `app_set_clock_source(CLOCK_SOURCE_PASSIVE)` at boot stores the Y3 crystal and resets the
    chip, while app_clock_init() falls back to RC and stores that when the crystal check fails: a possible reset loop
    with a flash write per boot. Replaced by RC32K (one reset if a crystal setting is stored); Y3 later (test032: needs
    the HBN pulls off and a start-up wait).
  - the low-power bring-up moved out of main() into a task started after the scheduler, 3 s after boot: the USB
    console is up first, so a hang in a low-power step still leaves `make flash` (reboot uart) working.
  - the crash record's HBN RAM address differs between firmwares (bl616_powertest 0x20010C40, bl616_lp 0x20010C00:
    .hbn_noinit is laid out by link order): a crash recorded by one firmware is not read by the other after a swap.
- 2026-11-01 ~16:40 UTC: with `CONFIG_BSP_USB=y` the LPAPP firmware's USB console works, and `lp_boot` ran every step
  (RC calibration 0x57A8801B applied, rfparam, Wi-Fi + BLE start), then USB was gone for good (no watchdog reset):
  the BLE controller's start calls btblecontroller_software_pds_reset(), which kills USB on this board (the product's
  known case, memory bl616-ble-pds-reset-kills-usb); the example does not override it. Manual ROM entry at ~17:00 UTC.
  Fixed by the same empty override in main.c: 17:01 UTC `lp_boot` -> "boot done", BLE address up, rc32k trim -840 ppm,
  console still there 10 s later (PASS of this step). Open: whether BLE sleep needs that PDS reset.
- the `lp_crumb` count field reads garbage (1817551176 marks): the count is not initialised on the first mark.
- the setup-mode build (lpfw_share reserves 30 KB of ram_wifi) does not link next to NetHub: bl616_powertest + LPAPP
  overflows ram_wifi by 30144 bytes. Design question: setup and operational as separate firmwares.
- VMware hands a new USB device to the VM ~4 s after it appears: a firmware that dies sooner is never seen in the VM.
- 2026-11-01 17:22 UTC, first BLE sleep current try (`ble_start_adv 0 0 0x0c80 0x0c80`, `wakeup_timer 120000 0`): USB-C 1
  meter 61 mA, the USB console never detached = never slept. `lp_sleepstat` (counter around the SDK's
  vApplicationSleep, -Wl,--wrap): the hook was never called. Cause: the test build added the product folder
  (firmware/bl616) to the include path before its own, so the kernel compiled against the product's FreeRTOSConfig.h
  (no configUSE_TICKLESS_IDLE; the same for lwipopts_user.h). Fixed: the test folder first (CMakeLists.txt). Any test
  firmware that shares the product folder must check which same-named header wins.
- the example's lp_exit() turned the sleep off on every BLE wake; on the BL616 every advertising event wakes the app,
  so it slept once (17:40:14 UTC) and stayed awake. Now only a connection or the window's end turns it off, and the
  USB console stays detached for the whole window (not re-attached on every 2 s wake).
- 2026-11-01 ~18:15-19:00 UTC, the product's BLE provisioning in the setup firmware (ble_pairing.c + wifi_check.c on
  the new firmware/bl616/wifi_local.c, pt_ble from bl616_powertest): `pt_ble on` never advertised (the phone's scan
  found nothing, awake). `lp_task ble` (the stuck task's saved context, addr2line): spinning in the controller's
  btble_controller_init -> rwip_init -> rwip_rand_init -> rwip_time_get, on bit 31 of 0x28000100 (the BLE core's
  "sample the native clock" bit, never cleared by the hardware). Not the cause, each tried: the start order against
  Wi-Fi (before/after fhost_init, concurrent as the example), the priority (3 / 15) and app_clock_init() right before,
  the BLE clock gate (CGEN_S3_BT_BLE2, bit 10; GLB_PER_Clock_UnGate() ignores the BLE clocks on the BL616), every
  CGEN gate open. The 17:51 build (the SDK example's start, CONFIG_BT_STACK_CLI=y) started BLE in the same firmware.
  Next A/B: CONFIG_BT_STACK_CLI back on.
- the crash recorder did not see it: only the `ble` task was stuck, everything else (the watchdog feed task too) ran.
  To add: a deadline on each start step (assert after N s) with the stuck task's saved PC and stack in the record.
- my own slip: one `make flash` ran on the old image after a failed build (`make; make flash`); build and flash
  chained since (memory check-build-before-use).
- help texts of SHELL_CMD_EXPORT_ALIAS must not contain an apostrophe (the macro breaks: "unterminated argument list").
