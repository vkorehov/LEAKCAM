# LEAKCAM BL616 full firmware

The BL616 is the board's power manager and radio. It runs one of two firmwares (DESIGN.md 5.3.1): this one, the full
firmware (operational: the K230, its Wi-Fi bridge, USB power), once presence is established; the setup firmware
(firmware/bl616_setup: BLE in low power) while it is not. **No BLE here** (user, 2026-11-02): each time BLE is needed
the board switches to the setup firmware (firmware/bl616_common/slot.c). The triggers (2026-11-02): no network stored, a check-in that
did not reach the server, "ble":1 in the check-in answer (switch_to_setup.c, called from full_boot.c and presence.c). Use case 1 (BLE provisioning)
moved to the setup firmware with it. Their shared modules are in firmware/bl616_common. The firmware is
written one use case at a time, each one
built from the code its test proved and re-proved by that test on this image (skill leakcam-product-firmware). The
first product firmware is in attic/firmware (reference only, not built).

## Use cases in this firmware

| # | Use case | Proven by | Files |
|---|---|---|---|
| 1 | Wi-Fi provisioning from the phone: moved to the setup firmware (firmware/bl616_setup, 2026-11-02); here only `provision` (the stored network as length + CRC, erase, the pairing PIN stored at flash time: firmware/bl616_common/provision_cmd.c) and the switch to setup without a network. | tests/test040, tests/test106 (to re-prove on the setup firmware) | full_boot.c, ../bl616_common/provision_cmd.c |

| 2 | More than one LEAKCAM: one device ID (the factory MAC) as USB serial number, in the setup firmware's BLE scan response and in every server check-in; from boot, on USB and on battery, the board checks in at the presence server every poll interval (the server's, 60 s by default) whenever a network is stored. The app opens on a status screen (its LEAKCAMs online/offline from the server, deletable), sets up only a board on USB power, gives a known one the short "change Wi-Fi" flow, and stores no Wi-Fi network. On battery the board sleeps in HBN between its events (hbn_sleep.c, tests/test141). | tests/test111 | device_id.c, presence.c, wifi_check.c (target + one run at a time); a failed check-in or "ble":1 -> the setup firmware (tests/test117, test119) |

The presence server is server/ (Go), on the Pi as the systemd unit leakcam-server (`server/deploy.sh`).

Always: the K230 is held off (k230_power.c), the watchdog and the `isp` entry (wdg_guard.c), the crash recorder
(crash_rec.c), memory statistics (mem_stat.c), the two-port USB console (usb_console2.c).

## Build and flash

```
cd firmware/bl616_full
make          # on an arm64 host the build runs in the K230 build container by itself (host.mk); sdk_patch.py first
make flash    # through PR1: the running firmware switches to the ROM itself (bl616_isp.py), no BOOT / EN
```

The image is `build/build_out/leakcam_full_bl616.bin`. A change to CMakeLists.txt, defconfig or Kconfig rebuilds the
whole SDK (about 10 min); other changes build incrementally.

## Console (PR1, /dev/ttyACM0)

| Command | Does |
|---|---|
| `provision` | the stored network as length + CRC-32 (never the passphrase), whether a pairing PIN is stored |
| `provision pin <6 digits>` | store the pairing PIN (make flash does it: tools/power-bringup/provision_pin.py) |
| `provision erase` | delete the stored network and the remembered AP |
| `presence` | the device ID, the server, the poll interval and the last check-in's state |
| `crash` / `crash clear` | the last crash record (exception, hang, assert, overflow, malloc) with its backtrace |
| `mem` | heap free / lowest / blocks: the leak check of every repeated operation |
| `isp` | watchdog off, USB detach, into the ROM for flashing |

## Rules for this code

- Only what is used: no unused function, variable, file, build option or script line in product files.
- Product only: test tools, test commands and test build steps live with the tests (tests/, tools/test-runner,
  tools/power-bringup), never here.
- Shared modules (firmware/bl616_common, only what both firmwares use: wifi_check.c, api.c, slot.c, provision_cmd.c, crash_rec.c, wdg_guard.c, mem_stat.c, usb_console2.c,
  device_id.c, the headers, bl616_leakcam.ld.in, host.mk, sdk_patch.py) are also built into the setup firmware and the
  test firmwares; wifi_link.c (here) into the power test firmware. A change to them is re-proved by each.
