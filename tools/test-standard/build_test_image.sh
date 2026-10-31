#!/bin/bash
# The K230 test image for the automated runs (run_tests.py): the product programs plus every test tool, one image so
# no test needs a reflash of its own (2026-10-25).
#   tools/test-standard/build_test_image.sh [SDK_DIR]     then firmware/k230_board/flash_nand.sh (USB boot)
# Adds to the SDK tree (none of it is in the product image):
#   kernel  tests/test017-nand-raw-speed/leakcam_bench.c -> board/ (ddr_bench, nand_bench, uart_bench, boot_times,
#           plt_write/plt_check, rtc_ppm/rtc_raw, pad_read, bat_watch); tests/test015 k230_bench.patch on the
#           bl616_nethub driver (bl616_bench, bl616_clk, bl616_dump)
#   /sdcard tests/test008 leakcam_cpubench, tests/test013 leakcam_kputest (LEAKCAM_BENCH=1), tests/test027 venctest,
#           tests/test025 memtest
set -e
SDK=${1:-$HOME/k230_rtos_sdk}
L=$(cd "$(dirname "$0")/../.." && pwd)
A=$SDK/src/applications
cp "$L/tests/test017-nand-raw-speed/leakcam_bench.c" "$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3/board/"
patch -d "$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/extdrv/bl616_nethub" -p1 --forward \
    < "$L/tests/test015-sdio-speed/k230_bench.patch"
mkdir -p "$A/leakcam/src"
cp "$L/tests/test008-minimal-image/leakcam_cpubench.c" "$L/tests/test013-kpu-change-net/leakcam_kputest.c" "$A/leakcam/src/"
cp "$L/attic/firmware/k230_capture/rtsmart/Makefile" "$A/leakcam/Makefile"
for t in "test027-video-encoder/venctest" "test025-ddr-stress/memtest"; do
    n=$(basename "$t")
    rm -rf "${A}_off/$n"
    mkdir -p "$A/$n" && cp "$L/tests/$t/"* "$A/$n/"
    grep -q "subdirs-y += $n" "$A/apps.mk" || echo "subdirs-y += $n" >> "$A/apps.mk"
done
cd "$SDK"
./leakcam_docker_build.sh log LEAKCAM_BENCH=1
O=$SDK/output/k230d_rtos_leakcam_defconfig
ls -l "$O"/*.kdimg "$O/images/sdcard/app"
