#!/usr/bin/env bash
# Install the LEAKCAM board (K230D + W25N02KV SPI NAND), the BL616 Wi-Fi driver and the
# leakcam programs into a k230_rtos_sdk checkout. Idempotent: rerun after editing anything in
# firmware/k230_board, firmware/k230_capture, firmware/k230_agent or firmware/bl616/wifi_ctrl_proto.h.
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

# AI2D driver log off: Canaan's ai_2d_log() calls rt_kprintf(fmt) without passing its arguments, so
# "<ai_2d> %s OK" reads a stray register as the string and the kernel faults at boot (Load Access Fault
# at 0x20 in rt_strlen, rev 1, 2026-10-18). Their boards survive by luck or build with RT_FASTBOOT.
AI2D=$RTT/drivers/interdrv/gnne/ai2d_dev.c
grep -q 'LEAKCAM: AI2D log off' "$AI2D" || python3 - "$AI2D" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '#define AI2D_LOG_ENABLE\n'
b = '/* LEAKCAM: AI2D log off (ai_2d_log drops its arguments and faults on "%s") */\n'
assert s.count(a) == 1, 'ai2d_dev.c changed, cannot apply the log patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: AI2D log off' "$AI2D" || { echo "AI2D patch not applied"; exit 1; }

# UFFS on the SPI NAND: nothing in the SDK mounts it (Canaan's mnt.c mounts rom + tmp, and their
# /sdcard is an SD card). Without this /bin and /sdcard are the empty romfs folders and the boot
# script finds no /sdcard/app/leakcam_agent (rev 1, 2026-10-18; a manual mount works).
MNT=$RTT/app_canmv/mnt.c
grep -q 'LEAKCAM: UFFS mounts' "$MNT" || python3 - "$MNT" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ('  if (dfs_mount(RT_NULL, "/dev/shm", "tmp", 0, 0) != 0) {\n'
     '    rt_kprintf("Dir /dev/shm mount failed!\\n");\n'
     '  }\n')
b = a + ('\n#ifdef RT_USING_DFS_UFFS\n'
         '  /* LEAKCAM: UFFS mounts, the NAND partitions of spinand_port.c (nand0 = bin, nand1 = sdcard) */\n'
         '  if (dfs_mount("nand0", "/bin", "uffs", 0, 0) != 0)\n'
         '    rt_kprintf("Dir /bin (nand0, uffs) mount failed!\\n");\n'
         '  if (dfs_mount("nand1", "/sdcard", "uffs", 0, 0) != 0)\n'
         '    rt_kprintf("Dir /sdcard (nand1, uffs) mount failed!\\n");\n'
         '#endif\n')
assert s.count(a) == 1, 'mnt.c changed, cannot apply the UFFS mount patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: UFFS mounts' "$MNT" || { echo "UFFS mount patch not applied"; exit 1; }

# SPI-NAND busy wait: Canaan's spinand_isbusy() abandoned page reads when its confirm read of the
# status still showed BUSY (rev 1, 2026-10-18: random "busy timeout, status 0x01", UFFS then used stale
# buffers and marked good blocks bad). Replaced by patches/spinand_isbusy.c: real-time limit, two
# consecutive "ready" reads.
SPN=$RTT/drivers/extdrv/spinand/spinand.c
python3 - "$SPN" "$HERE/rtsmart/patches/spinand_isbusy.c" <<'PY'
import sys
p, f = sys.argv[1], sys.argv[2]; s = open(p).read()
head = 'int spinand_isbusy(struct aic_spinand *flash, uint8_t *status)\n{\n'
tail = '    return SR & 0x1 ? -SPINAND_ERR_TIMEOUT : 0;\n}\n'
i = s.index(head)
j = s.index(tail, i)
old = s[i:j]
# Canaan's original or an earlier LEAKCAM version: replaced every run, so edits to the patch apply
assert s.count(head) == 1 and ('cnt = 1000' in old or 'LEAKCAM: wait on real time' in old), \
    'spinand.c changed, cannot apply the busy-wait patch'
open(p, 'w').write(s[:i] + open(f).read() + s[j + len(tail):])
PY
grep -q 'LEAKCAM: wait on real time' "$SPN" || { echo "busy-wait patch not applied"; exit 1; }

# poll(): RT-Thread's poll_do() never clears pt->triggered, so after one wake-up whose event leaves
# the fd not ready, poll_wait_timeout() skips the wait and poll() re-checks in a busy loop until the
# fd becomes ready, ignoring its timeout. The agent's poll() on the hook's stdout pipe spun at
# priority 25 from the moment the hook exited (the pipe's close wakes its readers): every user
# program, the NAND reads in it (26 KB/s instead of MB/s) and the idle thread starved (rev 1,
# 2026-10-18). Clear the flag before each check and wait only for the rest of the timeout.
POLL=$SDK/src/rtsmart/rtsmart/kernel/rt-thread/components/dfs/src/poll.c
grep -q 'LEAKCAM: poll re-arm' "$POLL" || python3 - "$POLL" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ('    while (1)\n'
     '    {\n'
     '        pf = fds;\n'
     '        num = 0;\n')
b = ('    /* LEAKCAM: poll re-arm: a wake-up without a ready fd sleeps again for the rest of msec */\n'
     '    rt_tick_t deadline = msec > 0 ? rt_tick_get() + rt_tick_from_millisecond(msec) : 0;\n'
     '\n'
     '    while (1)\n'
     '    {\n'
     '        pf = fds;\n'
     '        num = 0;\n'
     '        pt->triggered = 0;\n')
c = ('        if (poll_wait_timeout(pt, msec))\n'
     '            istimeout = 1;\n')
d = ('        int wait = msec;\n'
     '        if (msec > 0)\n'
     '        {\n'
     '            rt_int32_t left = (rt_int32_t)(deadline - rt_tick_get());\n'
     '            if (left <= 0)\n'
     '                break;\n'
     '            wait = (int)((rt_int64_t)left * 1000 / RT_TICK_PER_SECOND);\n'
     '            if (wait == 0)\n'
     '                wait = 1;\n'
     '        }\n'
     '        if (poll_wait_timeout(pt, wait))\n'
     '            istimeout = 1;\n')
assert s.count(a) == 1 and s.count(c) == 1, 'poll.c changed, cannot apply the poll re-arm patch'
open(p, 'w').write(s.replace(a, b).replace(c, d))
PY
grep -q 'LEAKCAM: poll re-arm' "$POLL" || { echo "poll patch not applied"; exit 1; }

# pipe poll: no POLLHUP when the last writer closed, so poll() never reported the end of a child's
# output (POSIX: POLLHUP once no writer is left; read() then returns 0)
PIPE=$SDK/src/rtsmart/rtsmart/kernel/rt-thread/components/drivers/src/pipe.c
grep -q 'LEAKCAM: pipe POLLHUP' "$PIPE" || python3 - "$PIPE" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ('        if (rt_ringbuffer_data_len(pipe->fifo) != 0)\n'
     '        {\n'
     '            mask |= POLLIN;\n'
     '        }\n')
b = a + ('        else if (pipe->writer == 0)\n'
         '        {\n'
         '            mask |= POLLHUP;    /* LEAKCAM: pipe POLLHUP, no writer left: end of file */\n'
         '        }\n')
assert s.count(a) == 1, 'pipe.c changed, cannot apply the POLLHUP patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: pipe POLLHUP' "$PIPE" || { echo "pipe patch not applied"; exit 1; }

# /bin/config.txt: Canaan's gen_config writes "auto_resize=y" (resize the SD card's data partition), and
# with no SD card the kernel prints "config command failed (-1)" at every boot. LEAKCAM has no SD card:
# write one key the kernel does not know ("board=leakcam", ignored silently; an empty file is left out
# of the UFFS image and a missing one prints "open /bin/config.txt failed"). The option itself stays on:
# app_canmv/main.c does not compile without it (format_data_partition).
GEN=$SDK/src/rtsmart/gen_config
grep -q 'LEAKCAM: empty config.txt' "$GEN" || python3 - "$GEN" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'cat <<EOF > ${SDK_BUILD_IMAGES_DIR}/bin/config.txt\nauto_resize=$CONFIG_RT_AUTO_RESIZE_PARTITION\nEOF\n'
b = '# LEAKCAM: empty config.txt (no SD card, nothing to resize); one unknown key, ignored\necho board=leakcam > ${SDK_BUILD_IMAGES_DIR}/bin/config.txt\n'
assert s.count(a) == 1, 'gen_config changed, cannot apply the config.txt patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: empty config.txt' "$GEN" || { echo "config.txt patch not applied"; exit 1; }

# BL616 Wi-Fi driver (NetHub over SDIO): sources + the control protocol header shared with the
# BL616 firmware; extdrv/SConscript picks up every subfolder, Kconfig needs one source line
DRV=$RTT/drivers/extdrv/bl616_nethub
rm -rf "$DRV"; mkdir -p "$DRV"
# files only: test/ holds the host tests and their RT-Thread stand-in headers
find "$HERE"/rtsmart/drivers/bl616_nethub -maxdepth 1 -type f -exec cp {} "$DRV/" \;
cp "$HERE/../bl616/wifi_ctrl_proto.h" "$DRV/"
python3 - "$RTT/drivers/extdrv/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
line = '    source "drivers/extdrv/bl616_nethub/Kconfig"\n'
if line not in s:
    a = '    source "drivers/extdrv/esp_hosted_mcu/Kconfig"\n'
    assert s.count(a) == 1, 'extdrv/Kconfig changed, cannot add the BL616 driver'
    open(p, 'w').write(s.replace(a, a + line))
PY

# RT-Thread SDIO core: the BL616 SDU function 1 CIS has no FUNCE tuple, so the core reads a
# max block size of 0 and refuses the function. Give it the 512-byte block of its port FIFO
# and the 200 ms enable timeout Bouffalo's own host driver sets for the same reason.
SDIOC=$SDK/src/rtsmart/rtsmart/kernel/rt-thread/components/drivers/sdio/sdio.c
grep -q 'LEAKCAM: BL616 SDU' "$SDIOC" || python3 - "$SDIOC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '        ret = sdio_read_cis(func);\n'
b = a + ('        /* LEAKCAM: BL616 SDU function 1 (424c:0606) has no FUNCE tuple */\n'
         '        if (ret == RT_EOK && !func->max_blk_size &&\n'
         '            ((func->manufacturer == 0x424c && func->product == 0x0606) ||\n'
         '             (!func->product && card->cis.manufacturer == 0x424c &&\n'
         '              card->cis.product == 0x0606)))\n'
         '        {\n'
         '            func->max_blk_size = 512;\n'
         '            if (!func->enable_timeout_val)\n'
         '                func->enable_timeout_val = 200;\n'
         '        }\n')
assert s.count(a) == 1, 'sdio.c changed, cannot apply the BL616 FUNCE patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: BL616 SDU' "$SDIOC" || { echo "SDIO FUNCE patch not applied"; exit 1; }

# 3. U-Boot/SPL: defconfig, device tree (reuses the BPI-Zero TARGET: same K230D SiP board code)
cp "$HERE/uboot/configs/k230d_leakcam_defconfig" "$UB/configs/"
cp "$HERE"/uboot/dts/k230d_leakcam.dts "$HERE"/uboot/dts/k230d_leakcam_pins.dtsi "$UB/arch/riscv/dts/"
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" ||
    sed -i 's|^dtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_canmv_bpi_zero.dtb$|&\ndtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_leakcam.dtb|' \
        "$UB/arch/riscv/dts/Makefile"
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" || { echo "could not register the dtb"; exit 1; }

# ISP tuning files: the SDK copies every ov5647-* resolution (3.3 MB) into /bin; the product runs 1280x960 only.
# UFFS mount time follows the used pages, so the other four sets cost ~85 ms per boot (tests/test021, 2026-10-21).
ISPMK=$SDK/src/rtsmart/mpp/userapps/src/sensor/Makefile
grep -q 'LEAKCAM: ov5647 1280x960 only' "$ISPMK" || python3 - "$ISPMK" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'ISP_SENSOR_CONFIG_PATTERNS += $(if $(filter y,$(CONFIG_MPP_ENABLE_SENSOR_OV5647)),ov5647-*)\n'
b = ('# LEAKCAM: ov5647 1280x960 only (the binned mode the cameras run)\n'
     'ISP_SENSOR_CONFIG_PATTERNS += $(if $(filter y,$(CONFIG_MPP_ENABLE_SENSOR_OV5647)),ov5647-1280x960*)\n')
assert s.count(a) == 1, 'sensor Makefile changed, cannot apply the ISP files patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: ov5647 1280x960 only' "$ISPMK" || { echo "ISP files patch not applied"; exit 1; }
rm -f "$SDK"/output/*/images/bin/ov5647-*                  # earlier builds' sets: the rule only adds files

# 4. the LEAKCAM apps (capture, history, stream): Makefile + Kconfig, sources copied into ./src
APP=$SDK/src/applications/leakcam
rm -rf "$APP"; mkdir -p "$APP/src"
cp "$CAP/rtsmart/Makefile" "$APP/Makefile"
cp "$CAP/rtsmart/Kconfig.app" "$APP/Kconfig"
( cd "$CAP" && cp -r --parents *.c *.cc *.h leakcam_change.kmodel third_party/miniz/miniz.c third_party/miniz/miniz.h \
      third_party/miniz/LICENSE third_party/opus "$APP/src/" )
# the BL616 link agent, with the protocol header it shares with the BL616 firmware
cp "$HERE/../k230_agent/leakcam_agent.c" "$HERE/../bl616/k230_link.h" "$APP/src/"
grep -q 'CONFIG_APP_ENABLE_LEAKCAM' "$SDK/src/applications/apps.mk" ||
    echo 'subdirs-$(CONFIG_APP_ENABLE_LEAKCAM) += leakcam' >> "$SDK/src/applications/apps.mk"

echo "LEAKCAM board installed into $SDK"
echo "next: cd $SDK && make k230d_rtos_leakcam_defconfig && make"
