# LEAKCAM BL616 shared modules

Only the modules **both** BL616 firmwares build (DESIGN.md 5.3.1): the full firmware (firmware/bl616_full: operational,
the K230 and its Wi-Fi bridge) and the setup firmware (firmware/bl616_setup: BLE in low power while there is no
presence). The test firmwares (tests/firmware/bl616_powertest, bl616_setup_test) build them too.

**Rule (user, 2026-11-02):** a module goes here only when both the full and the setup firmware use it. Used by one
firmware: in that firmware's folder (ble_pairing.c is the setup firmware's). Used only by tests: under tests/ (aht20.c
is the power test firmware's), never in firmware/. Shared with the K230 side only: firmware/k230_board (k230_link.h,
k230_linesplit.h).

| Module | Does |
|---|---|
| slot.c | `slot`, `slot switch`: the other firmware's slot made active, then reset (tests/test126) |
| api.c | the LEAKCAM API's check-in target (POST /v1/poll, the metrics in its form): the network check's last step and the presence check-in |
| metrics.c | every metric in the check-in (DESIGN.md 5.3.10): the sensors, uptime, boots, resets, heap, Wi-Fi, counters, the K230's last STATUS; HBN RAM; `metrics` |
| aht20.c | the AHT20 (U17) on I2C0: temperature and humidity (from the powertest firmware, tests/test021) |
| climate.c | the AHT20 every 10 min in both firmwares: the room's values only after 3 h cool, the 85 %RH alarm (the firmware's own action), `climate` (DESIGN.md 5.3.15, tests/test140) |
| rtc_clock.c | the HBN RTC as the time base through HBN sleep, wakes and resets; Y3 selected before the first sleep |
| wall_clock.c | the wall clock from the server's X-Time, kept against the RTC in HBN RAM |
| leakcam_version.cmake, version_gen.cmake | leakcam_version.h on every build: the image name and the product version (../VERSION + git commit) |
| wifi_check.c, wifi_check_calc.h | the credential check and the check-in run (join, DHCP, HTTP) through the wifi_link.h interface |
| wifi_link.h | that interface: the full firmware's wifi_link.c (NetHub bridge) or the setup firmware's wifi_setup.c (own station) |
| usb_console2.c | the two-port USB console (the BL616 shell, the K230 console) with USB detach/attach around the setup firmware's sleep |
| wdg_guard.c | the watchdog, `isp`, the feeding held during the setup firmware's sleep |
| crash_rec.c | the crash recorder (skill firmware-crash-recorder) |
| mem_stat.c | `mem`: heap free / lowest / blocks |
| device_id.c | the device ID (factory MAC) |
| provision_cmd.c | `provision`: the stored network (length + CRC), erase, the pairing PIN stored at flash time |
| board_pins.h, wifi_ctrl_proto.h | the board's pins; the Wi-Fi control protocol with the K230 (wifi_link.c, wifi_setup.c, the K230 driver) |
| bl616_leakcam.ld.in | the linker script (.hbn_noinit in HBN RAM) |
| host.mk, sdk_patch.py | the build; `make flash` writes both partition-table copies, `SLOT1_BIN=` an image into slot 1 (same version and commit), keeps the flashed ELFs in the K230 container and LEAKCAM's SDK changes, for every BL616 build |

Also: only what is used; a module here changes only with both firmwares and the test firmwares rebuilt.
