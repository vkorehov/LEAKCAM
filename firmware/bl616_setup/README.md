# LEAKCAM BL616 setup firmware

Runs while there is no working presence (DESIGN.md 5.3.1): the chip sleeps in PDS and advertises BLE every 2 s (every 100 ms while awake on USB power or after a phone's wake), so the
app can always reach the board; a phone sets the Wi-Fi (ble_pairing.c here, firmware/bl616_common/wifi_check.c).
USB-C 1 plugged in switches to the full firmware (firmware/bl616_full) when a network is stored. All setup activity
runs here, the normal product run in the full firmware (user, 2026-11-02): the board's only BLE, and the USB cable
setup (to come: the K230 powered with a relay image only while a phone is plugged in, tests/test113). Only what setup
mode needs.
Rules and obstacles: skill bl616-ble-low-power.

| File | Does |
|---|---|
| main.c | the start, then the sleep (or the switch to the full firmware when USB power is present) |
| setup_boot.c | the start's steps: low-power framework, RC32K, easyflash, RF, Wi-Fi and BLE in the proven order |
| setup_sleep.c | PDS between advertisements: the low-power callbacks, the clocks after a wake, the BLE-idle veto, the USB plug wake (IO3), a phone connection keeping it awake |
| switch_to_full.c | the switch to the full firmware: on a USB plug after the stored network's check up to the API, after new credentials once the phone left |
| ble_pairing.c | BLE provisioning: advertising as the device ID, PIN pairing, the credentials service; `ble_pairing_sleep_ok()` for the sleep (the board's only BLE: the full firmware has none) |
| ../bl616_common/slot.c | the switch to the other firmware's slot (shared) |
| wifi_setup.c | the wifi_link.h interface on the BL616's own station (no NetHub bridge) for the credential check |
| defconfig, FreeRTOSConfig.h, lwipopts_user.h, mbedtls_sample_config.h | its build: CONFIG_LPAPP, accurate tickless, BLE, no BLE shell commands |

Proven on rev 1 (tests/firmware/bl616_setup_test, which builds these sources): 99.4 % of the time in PDS at 2 s
advertising, the phone finds the sleeping board 10/10 (test120); a USB plug wakes it (test121).

The slot switch is firmware/bl616_common/slot.c (`slot`, `slot switch`; tests/test126); switch_to_full.c triggers it on a USB plug
once the stored network passes the check up to the API, and after new credentials (tests/test127). Not built yet: the
6 h retry (tests/test125) and the USB cable setup (tests/test113). No battery cutoff here: the cell's protection board
is the guard (bug-004, accepted).

## Build and flash

```
cd firmware/bl616_setup
make          # on an arm64 host the build runs in the K230 build container by itself (../bl616_common/host.mk)
make flash    # through PR1: the running firmware switches to the ROM itself
```

The image is `build/build_out/leakcam_setup_bl616.bin`.
