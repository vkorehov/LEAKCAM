#!/usr/bin/env bash
# Install the LEAKCAM board (K230D + W25N02KV SPI NAND) and the leakcam_capture app into a
# k230_rtos_sdk checkout. Idempotent: rerun after editing anything in firmware/k230_board or
# firmware/k230_capture.
#
#   firmware/k230_board/install.sh <k230_rtos_sdk>
#   cd <k230_rtos_sdk> && make k230d_rtos_leakcam_defconfig && make
set -euo pipefail
SDK=$(realpath "${1:?usage: install.sh <k230_rtos_sdk>}")
HERE=$(cd "$(dirname "$0")" && pwd)
CAP=$(realpath "$HERE/../k230_capture")
RTT=$SDK/src/rtsmart/rtsmart/kernel/bsp/maix3
UB=$SDK/src/uboot/uboot
for d in "$SDK/boards" "$RTT/board/configs" "$UB/arch/riscv/dts" "$SDK/src/applications"; do
    [ -d "$d" ] || { echo "not a k230_rtos_sdk tree (missing $d)"; exit 1; }
done

python3 "$HERE/pins.py" >/dev/null                       # regenerate the pin files first

# 1. SDK: defconfig, board folder, board choice
cp "$HERE/sdk/configs/k230d_rtos_leakcam_defconfig" "$SDK/configs/"
mkdir -p "$SDK/boards/k230d_leakcam"
cp "$HERE"/sdk/boards/k230d_leakcam/* "$SDK/boards/k230d_leakcam/"
python3 - "$SDK/boards/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
if 'BOARD_K230D_LEAKCAM' not in s:
    s = s.replace('    endchoice',
        '        config BOARD_K230D_LEAKCAM\n'
        '            bool "LEAKCAM K230D + BL616, SiP 128MiB LPDDR4, SPI NAND boot"\n'
        '            select BOARD_CHIP_K230D\n\n    endchoice', 1)
    s = s.replace('        default "k230d_evb" if BOARD_K230D_EVB\n',
        '        default "k230d_evb" if BOARD_K230D_EVB\n'
        '        default "k230d_leakcam" if BOARD_K230D_LEAKCAM\n', 1)
    s = s.replace('        default "K230D_EVB" if BOARD_K230D_EVB\n',
        '        default "K230D_EVB" if BOARD_K230D_EVB\n'
        '        default "LEAKCAM_K230D" if BOARD_K230D_LEAKCAM\n', 1)
    s = s.replace('    source "$(SDK_BOARDS_DIR)/k230_evb/Kconfig"\n',
        '    source "$(SDK_BOARDS_DIR)/k230_evb/Kconfig"\n'
        '    source "$(SDK_BOARDS_DIR)/k230d_leakcam/Kconfig"\n', 1)
    for key in ('config BOARD_K230D_LEAKCAM', '"k230d_leakcam" if', '"LEAKCAM_K230D" if',
                'k230d_leakcam/Kconfig'):
        assert key in s, 'boards/Kconfig layout changed, could not add: ' + key
    open(p, 'w').write(s)
PY

# 2. RT-Smart kernel: defconfig, board folder (pin mux, NAND partitions)
cp "$HERE/rtsmart/configs/k230d_leakcam_defconfig" "$RTT/configs/"
mkdir -p "$RTT/board/configs/k230d_leakcam"
cp "$HERE"/rtsmart/board/k230d_leakcam/* "$RTT/board/configs/k230d_leakcam/"

# board/pinmux.c pulls the board file in through #include TOSTRING(BOARD_CFG_FILE), which scons's
# dependency scanner does not follow: drop the old object so a changed pin table is recompiled
rm -f "$SDK"/output/k230d_rtos_leakcam_defconfig/rtsmart/kernel/board/pinmux.o

# RT-Smart SPI-NAND glue: UFFS gets at most the 64-byte user OOB. The W25N02KV has 128 spare
# bytes, but 64-127 are its on-die ECC parity, and UFFS allocates spare buffers of
# UFFS_MAX_SPARE_SIZE = 64 for 2 KiB pages while taking ecc_size = oob_size - oob_free. With 128
# that is 96 ECC bytes against 64-byte buffers; with 64 it is Canaan's tested EVB geometry.
PORT=$RTT/drivers/extdrv/spinand/spinand_port.c
grep -q 'LEAKCAM: UFFS user OOB' "$PORT" || python3 - "$PORT" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '        g_mtd_partitions[i].oob_size = flash->info->oob_size;\n'
b = ('        /* LEAKCAM: UFFS user OOB is at most 64 B (2 KiB pages; UFFS_MAX_SPARE_SIZE = 64) */\n'
     '        g_mtd_partitions[i].oob_size = flash->info->oob_size > 64 ? 64 : flash->info->oob_size;\n')
assert s.count(a) == 1, 'spinand_port.c changed, cannot apply the OOB patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: UFFS user OOB' "$PORT" || { echo "OOB patch not applied"; exit 1; }

# 3. U-Boot/SPL: defconfig, device tree (reuses the BPI-Zero TARGET: same K230D SiP board code)
cp "$HERE/uboot/configs/k230d_leakcam_defconfig" "$UB/configs/"
cp "$HERE"/uboot/dts/k230d_leakcam.dts "$HERE"/uboot/dts/k230d_leakcam_pins.dtsi "$UB/arch/riscv/dts/"
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" ||
    sed -i 's|^dtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_canmv_bpi_zero.dtb$|&\ndtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_leakcam.dtb|' \
        "$UB/arch/riscv/dts/Makefile"
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" || { echo "could not register the dtb"; exit 1; }

# 4. the LEAKCAM apps (capture, history, stream, audio): Makefile + Kconfig, sources copied into ./src
APP=$SDK/src/applications/leakcam
rm -rf "$APP"; mkdir -p "$APP/src"
cp "$CAP/rtsmart/Makefile" "$APP/Makefile"
cp "$CAP/rtsmart/Kconfig.app" "$APP/Kconfig"
( cd "$CAP" && cp -r --parents *.c *.cpp *.h third_party/miniz/miniz.c third_party/miniz/miniz.h \
      third_party/miniz/LICENSE "$APP/src/" )
grep -q 'CONFIG_APP_ENABLE_LEAKCAM' "$SDK/src/applications/apps.mk" ||
    echo 'subdirs-$(CONFIG_APP_ENABLE_LEAKCAM) += leakcam' >> "$SDK/src/applications/apps.mk"

echo "LEAKCAM board installed into $SDK"
echo "next: cd $SDK && make k230d_rtos_leakcam_defconfig && make"
