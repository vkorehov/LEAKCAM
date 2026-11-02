# LEAKCAM BL616 setup firmware

Runs while there is no working presence (DESIGN.md 5.3.1): the chip sleeps in PDS and advertises BLE every 2 s, so the
app can always reach the board; a phone sets the Wi-Fi as in the full firmware (firmware/bl616: ble_pairing.c,
wifi_check.c). USB-C 1 plugged in hands over to the full firmware (firmware/bl616_full). Only what setup mode needs.
Rules and obstacles: skill bl616-ble-low-power.

| File | Does |
|---|---|
| main.c | the start, then the sleep (or the hand-over when USB power is present) |
| setup_boot.c | the start's steps: low-power framework, RC32K, easyflash, RF, Wi-Fi and BLE in the proven order |
| setup_sleep.c | PDS between advertisements: the low-power callbacks, the clocks after a wake, the BLE-idle veto, the USB plug wake (IO3), a phone connection keeping it awake |
| wifi_setup.c | the wifi_link.h interface on the BL616's own station (no NetHub bridge) for the credential check |
| defconfig, FreeRTOSConfig.h, lwipopts_user.h, mbedtls_sample_config.h | its build: CONFIG_LPAPP, accurate tickless, BLE, no BLE shell commands |

Proven on rev 1 (tests/firmware/bl616_setup_test, which builds these sources): 99.4 % of the time in PDS at 2 s
advertising, the phone finds the sleeping board 10/10 (test120); a USB plug wakes it (test121).

Not built yet: the switch to the full firmware (the slot switch, DESIGN.md 5.3.1): on a USB plug, after new
credentials, and every 6 h for the full firmware's check-in (presence is proved there, not here). No battery cutoff
here: the cell's protection board is the guard (bug-004, accepted).

## Build and flash

```
cd firmware/bl616_setup
make          # on an arm64 host the build runs in the K230 build container by itself (../bl616/host.mk)
make flash    # through PR1: the running firmware switches to the ROM itself
```

The image is `build/build_out/leakcam_setup_bl616.bin`.
