# LEAKCAM BL616 product firmware

The BL616 is the board's power manager and radio. The product firmware is written one use case at a time, each one
built from the code its test proved and re-proved by that test on this image (skill leakcam-product-firmware). The
first product firmware is in attic/firmware (reference only, not built).

## Use cases in this firmware

| # | Use case | Proven by | Files |
|---|---|---|---|
| 1 | Wi-Fi provisioning from the phone: with USB power the BL616 advertises the provisioning service; the LEAKCAM Setup app sends a network; the BL616 checks it (join, address, internet) and stores it. No USB, no BLE. | tests/test040, tests/test106 | provision.c, ble_pairing.c, wifi_check.c, wifi_link.c, usb_power.c |

Always: the K230 is held off (k230_power.c), the watchdog and the `isp` entry (wdg_guard.c), the crash recorder
(crash_rec.c), memory statistics (mem_stat.c), the two-port USB console (usb_console2.c).

## Build and flash

```
cd firmware/bl616
make          # on an arm64 host the build runs in the K230 build container by itself (host.mk); sdk_patch.py first
make flash    # through PR1: the running firmware switches to the ROM itself (bl616_isp.py), no BOOT / EN
```

The image is `build/build_out/leakcam_bl616_bl616.bin`. A change to CMakeLists.txt, defconfig or Kconfig rebuilds the
whole SDK (about 10 min); other changes build incrementally.

## Console (PR1, /dev/ttyACM0)

| Command | Does |
|---|---|
| `prov` | USB power, the stored network as length + CRC-32 (never the passphrase) |
| `prov erase` | delete the stored network and the remembered AP |
| `crash` / `crash clear` | the last crash record (exception, hang, assert, overflow, malloc) with its backtrace |
| `mem` | heap free / lowest / blocks: the leak check of every repeated operation |
| `isp` | watchdog off, USB detach, into the ROM for flashing |

## Rules for this code

- Only what is used: no unused function, variable, file, build option or script line in product files.
- Product only: test tools, test commands and test build steps live with the tests (tests/, tools/test-standard,
  tools/power-bringup), never here.
- Shared modules (ble_pairing.c, wifi_check.c, wifi_link.c, crash_rec.c, wdg_guard.c, mem_stat.c, usb_console2.c,
  the headers, bl616_leakcam.ld.in, host.mk, sdk_patch.py) are also built into the BL616 test firmware; a change to
  them is re-proved by both.
