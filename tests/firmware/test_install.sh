#!/bin/bash
# All test firmware for the automated runs (run_tests.py), the tests' counterpart of firmware/k230_board/install.sh:
# install.sh (board support and SDK patches), then the K230 test image, then the BL616 test firmware.
#   tests/firmware/test_install.sh [SDK_DIR]     then firmware/k230_board/flash_nand.sh (USB boot) and
#                                                     make flash in tests/firmware/bl616_powertest
# The K230 test image: the programs the tests ran (tests/firmware/k230_apps: leakcam_agent, leakcam_wake,
# leakcam_stream, leakcam_hist, the change kmodel; a copy of the first product code, the attic is never read) plus
# every test tool, one image so no test needs a reflash of its own (2026-10-25). Adds to the SDK tree (none of it is
# in the product image):
#   kernel  CherryUSB device, CDC ACM on USB0 (test115); tests/test017-nand-raw-speed/leakcam_bench.c -> board/
#           (ddr_bench, nand_bench, uart_bench, usb_echo, boot_times,
#           plt_write/plt_check, rtc_ppm/rtc_raw, pad_read, bat_watch); tests/test015 k230_bench.patch on the
#           bl616_nethub driver (bl616_bench, bl616_clk, bl616_dump)
#   /sdcard tests/test008 leakcam_cpubench, tests/test013 leakcam_kputest (LEAKCAM_BENCH=1), tests/test027 venctest,
#           tests/test025 memtest
set -e
SDK=${1:-$HOME/k230_rtos_sdk}
L=$(cd "$(dirname "$0")/../.." && pwd)
A=$SDK/src/applications
"$L/firmware/k230_board/install.sh" "$SDK"
# test115: the K230's USB device on USB-C 1 (CherryUSB, CDC ACM ttyGS0, 1209:ABD1), in the test image only: appended
# to the SDK copy of the RT-Smart defconfig that install.sh just wrote (the product's stays without it)
cat >> "$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3/configs/k230d_leakcam_defconfig" <<'CFG'
CONFIG_ENABLE_CHERRY_USB=y
CONFIG_ENABLE_CHERRY_USB_DEVICE=y
CONFIG_CHERRY_USB_DEVICE_USING_DEV0=y
CONFIG_ENABLE_CHERRY_USB_DEVICE_CLASS_DRV=y
CONFIG_CHERRY_USB_DEVICE_ENABLE_CLASS_CDC_ACM=y
CONFIG_CHERRY_USB_DEVICE_FUNC_CDC=y
CFG
cp "$L/tests/test017-nand-raw-speed/leakcam_bench.c" "$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3/board/"
patch -d "$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/extdrv/bl616_nethub" -p1 --forward \
    < "$L/tests/test015-sdio-speed/k230_bench.patch"
# the test image programs: Makefile + Kconfig, sources copied into ./src (the Docker build only sees the SDK tree)
K=$L/tests/firmware/k230_apps
rm -rf "$A/leakcam"; mkdir -p "$A/leakcam/src"
cp "$K/rtsmart/Makefile" "$A/leakcam/Makefile"
cp "$K/rtsmart/Kconfig.app" "$A/leakcam/Kconfig"
( cd "$K" && cp -r --parents *.c *.cc *.h leakcam_change.kmodel third_party/miniz/miniz.c third_party/miniz/miniz.h \
      third_party/miniz/LICENSE third_party/opus "$A/leakcam/src/" )
cp "$L/firmware/bl616/k230_link.h" "$A/leakcam/src/"      # the link protocol shared with the BL616
cp "$L/tests/test008-minimal-image/leakcam_cpubench.c" "$L/tests/test013-kpu-change-net/leakcam_kputest.c" "$A/leakcam/src/"
grep -q 'CONFIG_APP_ENABLE_LEAKCAM' "$A/apps.mk" || echo 'subdirs-$(CONFIG_APP_ENABLE_LEAKCAM) += leakcam' >> "$A/apps.mk"
for t in "test027-video-encoder/venctest" "test025-ddr-stress/memtest"; do
    n=$(basename "$t")
    rm -rf "${A}_off/$n"
    mkdir -p "$A/$n" && cp "$L/tests/$t/"* "$A/$n/"
    [ -f "$A/$n/Kconfig" ] || : > "$A/$n/Kconfig"      # the SDK's defconfig sources a Kconfig from every app folder
    grep -q "subdirs-y += $n" "$A/apps.mk" || echo "subdirs-y += $n" >> "$A/apps.mk"
done
cd "$SDK"
./leakcam_docker_build.sh log LEAKCAM_BENCH=1
O=$SDK/output/k230d_rtos_leakcam_defconfig
ls -l "$O"/*.kdimg "$O/images/sdcard/app"
make -C "$L/tests/firmware/bl616_powertest"
ls -l "$L/tests/firmware/bl616_powertest/build/build_out/bl616_powertest_bl616.bin"
