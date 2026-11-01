# LEAKCAM BL616 shared modules

The modules both BL616 firmwares build (DESIGN.md 5.3.1): the full firmware (firmware/bl616_full: operational, the
K230 and its Wi-Fi bridge) and the setup firmware (firmware/bl616_setup: BLE in low power while there is no presence),
and the test firmwares that re-prove them (tests/firmware/bl616_powertest, bl616_setup_test).

| Module | Does |
|---|---|
| ble_pairing.c | BLE provisioning: advertising as the device ID, PIN pairing, the credentials service; `ble_pairing_sleep_ok()` for the setup firmware's sleep |
| wifi_check.c | the credential check and the check-in run (join, DHCP, HTTP) through the wifi_link.h interface |
| wifi_link.h | that interface: the full firmware's wifi_link.c (NetHub bridge) or the setup firmware's wifi_setup.c (own station) |
| usb_console2.c | the two-port USB console (the BL616 shell, the K230 console) with USB detach/attach around the setup firmware's sleep |
| wdg_guard.c | the watchdog, `isp`, the feeding held during the setup firmware's sleep |
| crash_rec.c | the crash recorder (skill firmware-crash-recorder) |
| mem_stat.c | `mem`: heap free / lowest / blocks |
| device_id.c | the device ID (factory MAC) |
| aht20.c | the humidity sensor (the power test firmware) |
| board_pins.h, k230_link.h, k230_linesplit.h, wifi_ctrl_proto.h | the board's pins and the K230 link protocols (shared with the K230 side) |
| bl616_leakcam.ld.in | the linker script (.hbn_noinit in HBN RAM) |
| host.mk, sdk_patch.py | the build in the K230 container and LEAKCAM's SDK changes, for every BL616 build |

Rules: only what is used; a module here changes only with both firmwares and the test firmwares rebuilt.
