# attic: the first product firmware (rev 1 bring-up era)

Moved here on 2026-10-31 (user): the product applications written before the board tests. The product is rewritten
from what the tests proved (skill leakcam-product-firmware); this code is reference only, not built.

| Path | Was |
|---|---|
| firmware/bl616/ | the BL616 power-manager application: main.c (HBN slices, wake decisions), aon_state.c, evlog.c, k230_link.c, k230_power.c, leak_wake.c, usb_power.c, its build files (CMakeLists.txt, Makefile, defconfig, Kconfig, FreeRTOSConfig.h), OpenOCD configs, host unit tests (test/), WIFI.md |
| firmware/k230_agent/ | leakcam_agent: the K230 side of the BL616 link (READY, WAKE, TIME, heartbeat, session end) |
| firmware/k230_capture/ | leakcam_wake: the wake algorithm (imgdiff, history views, change net, /v1/check, RTMP clips), mock_server.py, its host tests |
| firmware/k230_nn/ | the change-net training and kmodel compile (nncase) |

**Nothing builds from here** (user, 2026-10-31: the attic is fully decoupled). The K230 test image carries its own
copy of the programs the tests ran (tools/test-standard/k230_apps, installed by build_test_image.sh); docs that name
files here only say where the old code was.

What stayed in firmware/: everything the tests build from - the BL616 modules the test firmware compiles (wifi_link,
wifi_check, ble_pairing, aht20, crash_rec, wdg_guard, mem_stat, usb_console2), the shared headers (board_pins.h,
k230_link.h, k230_linesplit.h, ...), the linker script, host.mk, sdk_patch.py, lwipopts_user.h; and the K230 board
support (firmware/k230_board: install.sh SDK patches, link_mux and bl616_nethub drivers, flash_nand.sh).

The host tests in firmware/bl616/test/ were moved as a whole; their Makefile still points at sibling sources, so they
do not build from here.
