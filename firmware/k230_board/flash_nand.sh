#!/bin/sh
# Flash the LEAKCAM K230 image to the SPI NAND over USB-C 1: erase, then write.
#   firmware/k230_board/flash_nand.sh [SDK_DIR]        (K230 in USB boot mode, 29f1:0230)
# The burn loader does not erase what an image does not cover, and UFFS rejects a partition with
# leftover blocks (rev 1, 2026-10-18): always erase the whole image range (0 .. 248M) first.
# k230_flash_cli is x86-64 only: it runs in the K230 build container. --network host and /run/udev
# let libusb see the K230 re-enumerate as the loader (the VMware host passes it to Linux by itself since 2026-10-24).
set -e
SDK=${1:-$HOME/k230_rtos_sdk}
B=$SDK/tools/k230_flash/bin
O=$SDK/output/k230d_rtos_leakcam_defconfig
IMG=$O/LEAKCAM_K230D_rtsmart_local_nncase_v2.11.0.kdimg
# k230_flash_cli exits 0 when a write fails ("Write ... failed.", rev 1 2026-10-21: board lost power mid-write).
# A "usb bulk read data failed ... TIMEOUT" during the long erase is normal: the erase still ends "done".
run() {
    log=$(mktemp)
    docker run --rm --network host --privileged -v /dev/bus/usb:/dev/bus/usb -v /run/udev:/run/udev:ro \
        -v "$B:$B" -v "$O:$O" -w "$O" -e LD_LIBRARY_PATH="$B" k230-rtos-sdk-build:arm64-x86tc \
        timeout 300 "$B/k230_flash_cli" -m SPI_NAND "$@" 2>&1 | tee "$log"
    if grep -q -E "^(Write|Erase) .* failed" "$log"; then rm -f "$log"; echo "flash_nand.sh: FAILED" >&2; exit 1; fi
    rm -f "$log"
}
python3 "$(dirname "$0")/check_pad_voltage.py" "$O/uboot/spl/u-boot-spl.bin" "$O/uboot/u-boot.bin"
run --erase-medium --erase-address 0 --erase-size 0xF800000
run -f "$IMG"
echo "flashed $IMG; boot it with BOOT1 low (BL616: pt_boot1 low, pt_hold, pt_run)"
