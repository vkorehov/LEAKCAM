#!/bin/sh
# Flash the LEAKCAM K230 image to the SPI NAND over USB-C 1: erase, then write.
#   firmware/k230_board/flash_nand.sh [SDK_DIR]
# The K230 is put into its ROM's USB boot (29f1:0230) and switched off afterwards by the BL616, product or test
# firmware alike (tools/power-bringup/k230_usb_boot.py; the product's `k230 usbboot`, 2026-11-05).
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
PB=$(dirname "$0")/../../tools/power-bringup
python3 "$PB/k230_usb_boot.py" on
run --erase-medium --erase-address 0 --erase-size 0xF800000
run -f "$IMG"
python3 "$PB/k230_usb_boot.py" off
echo "flashed $IMG; the K230 is off (it boots from NAND at the next session)"
