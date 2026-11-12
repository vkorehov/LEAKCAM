#!/usr/bin/env bash
# Install the LEAKCAM board (K230D + W25N02KV SPI NAND) and the BL616 Wi-Fi driver into a
# k230_rtos_sdk checkout. Idempotent: rerun after editing anything in firmware/k230_board or
# firmware/bl616_common/wifi_ctrl_proto.h. All test firmware: tests/firmware/test_install.sh (runs this first).
#
#   firmware/k230_board/install.sh <k230_rtos_sdk>
#   cd <k230_rtos_sdk> && make k230d_rtos_leakcam_defconfig && make
set -euo pipefail
SDK=$(realpath "${1:?usage: install.sh <k230_rtos_sdk>}")
HERE=$(cd "$(dirname "$0")" && pwd)
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

# NAND ID: the kernel read it once and gave up, so one bad read lost /bin and /sdcard for the boot (test051,
# 2026-10-30: "unknown raw ID aa220000", Winbond's ef aa 22 one byte late, SPL had read it fine; 0 in 24 boots
# since, cause open: tests/test017 HISTORY). Reset and read again up to 3 times; each retry is logged.
grep -q 'LEAKCAM: ID retry' "$SPN" || python3 - "$SPN" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ('    if ((result = spinand_info_read(flash)) != SPINAND_SUCCESS) {\n'
     '        goto exit_spinand_init;\n'
     '    }\n')
b = ('    /* LEAKCAM: ID retry: reset and read the ID again, each retry logged */\n'
     '    for (int try = 1; (result = spinand_info_read(flash)) != SPINAND_SUCCESS; try++) {\n'
     '        if (try == 3)\n'
     '            goto exit_spinand_init;\n'
     '        LOG_W("ID retry %d\\n", try);\n'
     '        if ((result = spinand_reset(flash)) != SPINAND_SUCCESS)\n'
     '            goto exit_spinand_init;\n'
     '    }\n')
assert s.count(a) == 1, 'spinand.c changed, cannot add the ID retry'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: ID retry' "$SPN" || { echo "NAND ID retry patch not applied"; exit 1; }

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
cp "$HERE/../bl616_common/wifi_ctrl_proto.h" "$DRV/"
python3 - "$RTT/drivers/extdrv/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
line = '    source "drivers/extdrv/bl616_nethub/Kconfig"\n'
if line not in s:
    a = '    source "drivers/extdrv/esp_hosted_mcu/Kconfig"\n'
    assert s.count(a) == 1, 'extdrv/Kconfig changed, cannot add the BL616 driver'
    open(p, 'w').write(s.replace(a, a + line))
PY

# Link mux (2026-10-22): the console and the BL616 link frames share UART1; the BL616 shows the console on its
# second USB port. Sources + the splitter and link protocol headers shared with the BL616 firmware.
MUX=$RTT/drivers/extdrv/link_mux
rm -rf "$MUX"; mkdir -p "$MUX"
cp "$HERE"/rtsmart/drivers/link_mux/* "$MUX/"
cp "$HERE/k230_linesplit.h" "$HERE/k230_link.h" "$MUX/"
python3 - "$RTT/drivers/extdrv/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
line = '    source "drivers/extdrv/link_mux/Kconfig"\n'
if line not in s:
    a = '    source "drivers/extdrv/bl616_nethub/Kconfig"\n'
    assert s.count(a) == 1, 'extdrv/Kconfig changed, cannot add the link mux'
    open(p, 'w').write(s.replace(a, a + line))
PY

# /dev/ota (DESIGN.md 5.3.7, 2026-11-06): the A/B image update on SPI NAND, the slot and record writer.
OTA=$RTT/drivers/extdrv/ota_nand
rm -rf "$OTA"; mkdir -p "$OTA"
cp "$HERE"/rtsmart/drivers/ota_nand/* "$OTA/"
python3 - "$RTT/drivers/extdrv/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
line = '    source "drivers/extdrv/ota_nand/Kconfig"\n'
if line not in s:
    a = '    source "drivers/extdrv/link_mux/Kconfig"\n'
    assert s.count(a) == 1, 'extdrv/Kconfig: no link_mux line to add ota_nand after'
    open(p, 'w').write(s.replace(a, a + line))
PY

# /dev/sysstat (DESIGN.md 5.3.10, 2026-11-06): the kernel heap and pages for the program's STATUS frame.
SST=$RTT/drivers/extdrv/sys_stat
rm -rf "$SST"; mkdir -p "$SST"
cp "$HERE"/rtsmart/drivers/sys_stat/* "$SST/"
python3 - "$RTT/drivers/extdrv/Kconfig" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
line = '    source "drivers/extdrv/sys_stat/Kconfig"\n'
if line not in s:
    a = '    source "drivers/extdrv/ota_nand/Kconfig"\n'
    assert s.count(a) == 1, 'extdrv/Kconfig: no ota_nand line to add sys_stat after'
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

# RT-Thread SDIO core: sdio_attach_irq() enables the function in the card's CCCR but never the host's card
# interrupt (Linux does on the first claim); the K230 SDHCI arms only the signal-enable half at init, so no card
# interrupt ever arrived and every BL616 message waited for the driver's 1 s safety poll (tests/test100, 2026-10-25:
# card irqs 0, DHCP 2.8 s, join +1 s per step). Arm it when the first function IRQ is attached.
grep -q 'LEAKCAM: arm the host card interrupt' "$SDIOC" || python3 - "$SDIOC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '    host->sdio_irq_num++;\n    return RT_EOK;\n'
b = ('    if (host->sdio_irq_num++ == 0 && (host->flags & MMCSD_SUP_SDIO_IRQ))  /* LEAKCAM: arm the host card interrupt */\n'
     '        host->ops->enable_sdio_irq(host, 1);\n'
     '    return RT_EOK;\n')
assert s.count(a) == 1, 'sdio.c changed, cannot arm the host card interrupt'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: arm the host card interrupt' "$SDIOC" || { echo "SDIO card interrupt patch not applied"; exit 1; }

# lwIP: no ARP probe of the offered address after the DHCP ACK; it held the address back by up to 1 s (500 ms
# timeouts on the 500 ms DHCP timer, tests/test100 2026-10-25). The lease is the BL616's own (same MAC).
LWOPT=$SDK/src/rtsmart/rtsmart/kernel/rt-thread/components/net/lwip-2.1.2/src/lwipopts.h
grep -q 'LEAKCAM: no DHCP ARP check' "$LWOPT" || python3 - "$LWOPT" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '#define DHCP_DOES_ARP_CHECK         (LWIP_DHCP)\n'
assert s.count(a) == 1, 'lwipopts.h changed, cannot drop the DHCP ARP check'
open(p, 'w').write(s.replace(a, '#define DHCP_DOES_ARP_CHECK         0   /* LEAKCAM: no DHCP ARP check */\n'))
PY
grep -q 'LEAKCAM: no DHCP ARP check' "$LWOPT" || { echo "lwIP DHCP ARP check patch not applied"; exit 1; }
# lwIP's heap: lwipopts.h leaves MEM_SIZE undefined, so lwIP's default 1600 B: TCP copies what a socket sends into it,
# and the two cameras' video uploads (2 x 1.7 Mbit/s) failed with "Out of memory" (tests/test147, firmware/VIDEO.txt
# 2026-11-12); the link itself carries 27.5 Mbit/s up (tests/test100, iperf, one connection)
grep -q 'LEAKCAM: lwIP heap' "$LWOPT" || python3 - "$LWOPT" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '//#define MEM_SIZE                    (1024*64)\n'
assert s.count(a) == 1, 'lwipopts.h changed, cannot size the lwIP heap'
open(p, 'w').write(s.replace(a, '#define MEM_SIZE                    (256 * 1024)   /* LEAKCAM: lwIP heap */\n'))
PY
grep -q 'LEAKCAM: lwIP heap' "$LWOPT" || { echo "lwIP heap patch not applied"; exit 1; }

# 3. U-Boot/SPL: defconfig, device tree (reuses the BPI-Zero TARGET: same K230D SiP board code)
cp "$HERE/uboot/configs/k230d_leakcam_defconfig" "$UB/configs/"
cp "$HERE"/uboot/dts/k230d_leakcam.dts "$HERE"/uboot/dts/k230d_leakcam_pins.dtsi "$UB/arch/riscv/dts/"

# SPL: the A/B slot record (ota_meta) from SPI NAND (DESIGN.md 5.3.7, tests/test130). Canaan's k230_read_meta_block()
# reads it from SD only and answers -1 on NAND: both records "invalid", so the SPL always booted slot A and the OTA's
# record never chose a slot (found 2026-11-06). The record's 512 bytes at its offset (in its own erase block: a bad one
# = no record); a corrected bit flip (-EUCLEAN) still reads the data.
BOOTC=$UB/board/kendryte/common/k230_boot.c
grep -q 'LEAKCAM: ota_meta from NAND' "$BOOTC" || python3 - "$BOOTC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
head = 'static int k230_read_meta_block(ulong offset, void *buf)\n{\n'
i = s.find(head)
assert i >= 0 and s.count(head) == 1, 'k230_boot.c changed, cannot apply the ota_meta patch'
tail = '    return -1;\n}\n'
j = s.find(tail, i)
assert j > i, 'k230_read_meta_block() end not found'
nand = ('#if defined(CONFIG_MTD_SPI_NAND)\n'
        '    /* LEAKCAM: ota_meta from NAND: the record at its offset, in its own erase block (a bad one = no record) */\n'
        '    if (g_boot_medium == BOOT_MEDIUM_NANDFLASH) {\n'
        '        struct mtd_info *mtd = get_mtd_by_name(SPINAND_NAME);\n'
        '        size_t retlen = 0;\n'
        '        int r;\n\n'
        '        if (IS_ERR_OR_NULL(mtd))\n'
        '            return -1;\n'
        '        r = mtd_block_isbad(mtd, offset) ? -1 : mtd_read(mtd, offset, 512, &retlen, buf);\n'
        '        put_mtd_device(mtd);\n'
        '        return ((r == 0 || r == -EUCLEAN) && retlen == 512) ? 0 : -1;\n'
        '    }\n'
        '#endif\n\n')
open(p, 'w').write(s[:j] + nand + s[j:])
PY
grep -q 'LEAKCAM: ota_meta from NAND' "$BOOTC" || { echo "ota_meta NAND patch not applied"; exit 1; }

# U-Boot/SPL RX sample delay step (tests/test017, 2026-10-24): Canaan's designware_spi.c computes the step as
# NSEC_PER_SEC / bus clock in whole ns - 1 ns at the 800 MHz SPI0 clock instead of 1.25 - so the dts's 6 ns gave delay
# 6, not the 5 the kernel uses (RT_SPI0_RXSD). The step in ps: 6 ns -> 5. (A per-phase 100 MHz data clock was tried
# here and dropped: test017 README.)
DWS=$UB/drivers/spi/designware_spi.c
grep -q 'LEAKCAM: delay step in ps' "$DWS" || python3 - "$DWS" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ('\tpriv->def_rxdly = DIV_ROUND_CLOSEST(plat->def_rxdly_ns,\n'
     '\t\t\t\t\t\t\tNSEC_PER_SEC /\n'
     '\t\t\t\t\t\t\tpriv->bus_clk_rate);\n')
assert s.count(a) == 1, 'designware_spi.c changed, cannot apply the delay step fix'
s = s.replace(a, '\tpriv->def_rxdly = DIV_ROUND_CLOSEST(plat->def_rxdly_ns * 1000,   /* LEAKCAM: delay step in ps */\n'
                 '\t\t\t\t\t    DIV_ROUND_CLOSEST(1000000000000ULL, priv->bus_clk_rate));\n')
open(p, 'w').write(s)
PY
grep -q 'LEAKCAM: delay step in ps' "$DWS" || { echo "U-Boot delay step fix not applied"; exit 1; }

# SPL and OpenSBI print on UART1 at the link's rate (2 Mbaud), as RT-Smart does since the link mux (tests/test039):
# their console reaches the BL616's /dev/ttyACM1 from the first SPL line. U-Boot's ns16550 has neither the K230's
# UART clock divider nor the DesignWare fractional divisor the link rates need (100 MHz / 16 / 2 M = 3 + 2/16):
# probe sets UART1's clock divider 1/2 -> 1/1 (sysctl uart_i2c_clkdiv_cfg 0x9110002c bits 5:3, bit 31 = write
# enable, as RT-Smart's sysctl_clk_set_leaf_div), setbrg writes the fraction (DLF, 4 bits at 0xC0) as drv_uart.
NS=$UB/drivers/serial/ns16550.c
grep -q 'LEAKCAM: UART1 clock' "$NS" || python3 - "$NS" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '\tcom_port->plat = dev_get_plat(dev);\n\tns16550_init(com_port, -1);\n'
b = ('\tcom_port->plat = dev_get_plat(dev);\n'
     '\tif (plat->base == 0x91401000) {\t/* LEAKCAM: UART1 clock 1/1 = 100 MHz (console + BL616 link, 2 Mbaud) */\n'
     '\t\tu32 v = readl((void *)0x9110002c);\n'
     '\t\twritel((v & ~0x38u) | (1u << 31), (void *)0x9110002c);\n'
     '\t}\n'
     '\tns16550_init(com_port, -1);\n')
assert s.count(a) == 1, 'ns16550 probe changed, cannot set the UART1 clock'
s = s.replace(a, b)
a = '\tclock_divisor = ns16550_calc_divisor(com_port, plat->clock, baudrate);\n\n\tns16550_setbrg(com_port, clock_divisor);\n\n\treturn 0;\n}\n'
b = ('\t/* LEAKCAM: DesignWare fractional divisor (DLF, 4 bits at 0xC0): divisor x 16 rounded, the integer part in\n'
     '\t * DLL/DLH, the sixteenths in DLF (2 Mbaud from 100 MHz = 3 + 2/16), as RT-Smart\'s drv_uart */\n'
     '\tunsigned int x16 = (plat->clock + baudrate / 2) / baudrate;\n'
     '\tclock_divisor = x16 / 16 ? x16 / 16 : 1;\n'
     '\tns16550_setbrg(com_port, clock_divisor);\n'
     '\tint lcr = serial_in(&com_port->lcr);\n'
     '\tserial_out(lcr | UART_LCR_DLAB, &com_port->lcr);\n'
     '\twritel(x16 / 16 ? x16 % 16 : 0, (void *)(plat->base + 0xc0));\n'
     '\tserial_out(lcr, &com_port->lcr);\n\n'
     '\treturn 0;\n}\n')
assert s.count(a) == 1, 'ns16550 setbrg changed, cannot add the fractional divisor'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: UART1 clock' "$NS" || { echo "ns16550 patch not applied"; exit 1; }

# OpenSBI: its 8250 driver already writes DLF; with the clock SPL set it is 100 MHz, not the SDK's fixed 50 MHz
OSBI=$SDK/src/opensbi/opensbi/platform/kendryte/fpgac908/platform.c
grep -q 'LEAKCAM: UART clock' "$OSBI" || python3 - "$OSBI" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '#define UART_CLK                    50000000\n'
b = '#define UART_CLK                    100000000   /* LEAKCAM: UART clock 1/1, set by SPL (ns16550 patch) */\n'
assert s.count(a) == 1, 'OpenSBI platform.c changed, cannot set the UART clock'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: UART clock' "$OSBI" || { echo "OpenSBI UART clock patch not applied"; exit 1; }
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" ||
    sed -i 's|^dtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_canmv_bpi_zero.dtb$|&\ndtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_leakcam.dtb|' \
        "$UB/arch/riscv/dts/Makefile"
grep -q 'k230d_leakcam.dtb' "$UB/arch/riscv/dts/Makefile" || { echo "could not register the dtb"; exit 1; }

# KPU driver: gnne_setup_kpu_bandwidth() writes its two registers at their physical address (0x91301d8c) with the
# rt_ioremap version disabled by "#if 0". At -O1 it happened to work; at -O2 the kernel faults there at every boot
# (Store/AMO page fault, tests/test008, 2026-10-22). Use the mapped version.
GNNE=$RTT/drivers/interdrv/gnne/gnne_dev.c
grep -q 'LEAKCAM: KPU bandwidth mapped' "$GNNE" || python3 - "$GNNE" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'static int gnne_setup_kpu_bandwidth(void)\n{\n#if 0\n'
b = 'static int gnne_setup_kpu_bandwidth(void)\n{\n#if 1   /* LEAKCAM: KPU bandwidth mapped (the physical write faults at -O2) */\n'
assert s.count(a) == 1, 'gnne_dev.c changed, cannot apply the KPU bandwidth patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: KPU bandwidth mapped' "$GNNE" || { echo "KPU bandwidth patch not applied"; exit 1; }

# Kernel optimisation: the SDK's release build is -O1 (rtconfig.py). Speed first (tests/test008): the level that
# measures fastest; -g stays (debug info only in the ELF, not in rtthread.bin).
KOPT=-O1        # -O2 crashes in the LWP signal return (tests/test008); back to the SDK level until fixed
RTCPY=$RTT/rtconfig.py
python3 - "$RTCPY" "$KOPT" <<'PY'
import re, sys
p, opt = sys.argv[1], sys.argv[2]; s = open(p).read()
s2, n = re.subn(r"CFLAGS \+= ' -O[0-3s] -g -gdwarf-2'(  # LEAKCAM: kernel opt)?", f"CFLAGS += ' {opt} -g -gdwarf-2'  # LEAKCAM: kernel opt", s)
assert n == 1, 'rtconfig.py changed, cannot set the kernel optimisation'
open(p, 'w').write(s2)
PY
grep -q "LEAKCAM: kernel opt" "$RTCPY" || { echo "kernel optimisation patch not applied"; exit 1; }

# Time zone: RT-Thread's musl sys/time.h turns its lightweight time zone on unconditionally with a +8 h default
# (China), so localtime()/ctime() and msh `date` read 8 h ahead of the UTC RTC. LEAKCAM runs on UTC only.
TZC=$RTT/../../rt-thread/components/libc/compilers/musl/time.c
grep -q 'LEAKCAM: UTC' "$TZC" || python3 - "$TZC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '#define RT_LIBC_TZ_DEFAULT_HOUR   (8U)\n'
b = '#define RT_LIBC_TZ_DEFAULT_HOUR   (0U)   /* LEAKCAM: UTC */\n'
assert s.count(a) == 1, 'musl time.c changed, cannot apply the UTC patch'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: UTC' "$TZC" || { echo "UTC patch not applied"; exit 1; }

# BL616 link: UART1 registers at LINK_BAUD of k230_board/k230_link.h (the one source for both chips; tests/test039: 2 Mbaud,
# 4 Mbaud lost ~0.5 % of short writes after an idle gap). The serial layer has no termios: the agent cannot set it.
UARTC=$RTT/drivers/interdrv/uart/drv_uart.c
LINK_BAUD=$(awk '/^#define LINK_BAUD/ {print $3}' "$HERE/k230_link.h")
[ -n "$LINK_BAUD" ] || { echo "LINK_BAUD not found in k230_board/k230_link.h"; exit 1; }
grep -q 'LEAKCAM: link baud' "$UARTC" || python3 - "$UARTC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '        rt_snprintf(dev_name, sizeof(dev_name), "uart%d", inst->uart.index);\n        rt_hw_interrupt_install('
b = ('        if (inst->uart.index == 1)\n'
     '            inst->device.config.baud_rate = 2000000;   /* LEAKCAM: link baud (k230_board/k230_link.h LINK_BAUD) */\n'
     + a)
assert s.count(a) == 1, 'drv_uart.c changed, cannot set the link baud'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: link baud' "$UARTC" || { echo "link baud patch not applied"; exit 1; }
sed -i "s|baud_rate = [0-9]*;   /\* LEAKCAM: link baud|baud_rate = $LINK_BAUD;   /* LEAKCAM: link baud|" "$UARTC"
grep -q "baud_rate = $LINK_BAUD;   /\* LEAKCAM: link baud" "$UARTC" || { echo "link baud not set to $LINK_BAUD"; exit 1; }

# test039 (2026-10-27): typed console commands got lost whole - the driver drops the entire RX FIFO on a framing,
# parity or overrun error. Count each kind and the bytes dropped (uart_rx_err[], link_mux's link_stat prints them).
grep -q 'LEAKCAM: rx error counters' "$UARTC" || python3 - "$UARTC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'static void drv_uart_irq_handler(int irq, void* param)\n{'
b = ('/* LEAKCAM: rx error counters (tests/test039): [0] framing, [1] parity, [2] overrun, [3] bytes dropped, [4] last LSR */\n'
     'volatile rt_uint32_t uart_rx_err[5];\n\n' + a)
c = '_on_error_drop_fifo:\n    /* Drain FIFO on error */\n    while (lsr & UART_LSR_DR) {\n        (void)read32(inst->reg + UART_RBR);\n'
d = ('_on_error_drop_fifo:\n'
     '    if (inst->uart.index == 1) {   /* LEAKCAM: rx error counters */\n'
     '        uart_rx_err[0] += !!(lsr & UART_LSR_FE);\n'
     '        uart_rx_err[1] += !!(lsr & UART_LSR_PE);\n'
     '        uart_rx_err[2] += !!(lsr & UART_LSR_OE);\n'
     '        uart_rx_err[4] = lsr;\n'
     '    }\n'
     '    /* Drain FIFO on error */\n    while (lsr & UART_LSR_DR) {\n        (void)read32(inst->reg + UART_RBR);\n'
     '        if (inst->uart.index == 1)\n            uart_rx_err[3]++;\n')
assert s.count(a) == 1 and s.count(c) == 1, 'drv_uart.c changed, cannot add the rx error counters'
open(p, 'w').write(s.replace(a, b).replace(c, d))
PY
grep -q 'LEAKCAM: rx error counters' "$UARTC" || { echo "rx error counters patch not applied"; exit 1; }
grep -q 'LEAKCAM: rx byte counter' "$UARTC" || python3 - "$UARTC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = '    if (!(read32(uart_base + UART_LSR) & UART_LSR_DR)) {\n        return -1;\n    }\n\n    return (int)read32(uart_base + UART_RBR);\n}'
b = ('    if (!(read32(uart_base + UART_LSR) & UART_LSR_DR)) {\n        return -1;\n    }\n\n'
     '    if (inst->uart.index == 1)   /* LEAKCAM: rx byte counter (tests/test039: bytes taken from the FIFO) */\n'
     '        uart_rx_err[5]++;\n'
     '    return (int)read32(uart_base + UART_RBR);\n}')
c = 'volatile rt_uint32_t uart_rx_err[5];'
assert s.count(a) == 1 and s.count(c) == 1, 'drv_uart.c changed, cannot add the rx byte counter'
open(p, 'w').write(s.replace(a, b).replace(c, 'volatile rt_uint32_t uart_rx_err[6];   /* [5] bytes read from the FIFO */'))
PY
grep -q 'LEAKCAM: rx byte counter' "$UARTC" || { echo "rx byte counter patch not applied"; exit 1; }
grep -q 'LEAKCAM: reset and irq counters' "$UARTC" || python3 - "$UARTC" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
c = 'volatile rt_uint32_t uart_rx_err[6];   /* [5] bytes read from the FIFO */'
c2 = ('volatile rt_uint32_t uart_rx_err[12];  /* [5] bytes read from the FIFO, [6] resets (configure/close), [7] irqs, '
      '[8] irq rx data, [9] irq char timeout, [10] irq line status, [11] irq other (LEAKCAM: reset and irq counters) */')
a = '    // Perform full UART reset (clears RX/TX FIFO, resets internal logic)\n    write32(uart_base + UART_SRR, UART_SRR_UR | UART_SRR_RFR | UART_SRR_XFR);\n'
a2 = ('    if (inst->uart.index == 1)\n        uart_rx_err[6]++;\n' + a)
d = '    uint8_t lsr = (uint8_t)read32(inst->reg + UART_LSR);\n\n    /* Data available interrupt */'
d2 = ('    uint8_t lsr = (uint8_t)read32(inst->reg + UART_LSR);\n'
      '    if (inst->uart.index == 1) {\n'
      '        uart_rx_err[7]++;\n'
      '        uart_rx_err[iir == UART_IIR_IID_RECV_DATA ? 8 : iir == UART_IIR_IID_CHARTO ? 9 : iir == UART_IIR_IID_RECV_LINE_STAT ? 10 : 11]++;\n'
      '    }\n\n    /* Data available interrupt */')
assert s.count(c) == 1 and s.count(d) == 1 and s.count(a) >= 1, 'drv_uart.c changed, cannot add the reset and irq counters'
open(p, 'w').write(s.replace(c, c2).replace(a, a2).replace(d, d2))
PY
grep -q 'LEAKCAM: reset and irq counters' "$UARTC" || { echo "reset and irq counters patch not applied"; exit 1; }

# ISP tuning files: the SDK copies every ov5647-* resolution (3.3 MB) into /bin; the product runs the native
# 2592x1944 only (2026-11-12; 1280x960 before).
# UFFS mount time follows the used pages, so the other four sets cost ~85 ms per boot (tests/test022, 2026-10-21).
ISPMK=$SDK/src/rtsmart/mpp/userapps/src/sensor/Makefile
grep -q 'LEAKCAM: ov5647 2592x1944 only' "$ISPMK" || python3 - "$ISPMK" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
b = ('# LEAKCAM: ov5647 2592x1944 only (the native mode the cameras run)\n'
     'ISP_SENSOR_CONFIG_PATTERNS += $(if $(filter y,$(CONFIG_MPP_ENABLE_SENSOR_OV5647)),ov5647-2592x1944*)\n')
# the SDK's line, or the earlier LEAKCAM 1280x960 one (an SDK tree patched before 2026-11-12)
pat = re.compile(r'(# LEAKCAM: ov5647 1280x960 only[^\n]*\n)?ISP_SENSOR_CONFIG_PATTERNS \+= \$\(if \$\(filter y,'
                 r'\$\(CONFIG_MPP_ENABLE_SENSOR_OV5647\)\),ov5647-[^)]*\)\n')
assert len(pat.findall(s)) == 1, 'sensor Makefile changed, cannot apply the ISP files patch'
open(p, 'w').write(pat.sub(lambda m: b, s))
PY
grep -q 'LEAKCAM: ov5647 2592x1944 only' "$ISPMK" || { echo "ISP files patch not applied"; exit 1; }
# a test image's extra 1280x960 set (tests/firmware/test_install.sh adds it after this script): out for the product
sed -i '/LEAKCAM test image: ov5647 1280x960/,+1d' "$ISPMK"
rm -f "$SDK"/output/*/images/bin/ov5647-*                  # earlier builds' sets: the rule only adds files

# The product program (firmware/k230_app): src/applications/leakcam, sources in ./src (the Docker build only sees the
# SDK tree), with the link protocol it shares with the BL616. tests/firmware/test_install.sh replaces it with the test
# programs.
A=$SDK/src/applications
rm -rf "$A/leakcam"; mkdir -p "$A/leakcam/src"
cp "$HERE/../k230_app/rtsmart/Makefile" "$HERE/../k230_app/rtsmart/rom_c.py" "$A/leakcam/"
cp "$HERE/../k230_app/rtsmart/Kconfig.app" "$A/leakcam/Kconfig"
cp "$HERE"/../k230_app/*.c "$HERE"/../k230_app/*.h "$HERE/k230_link.h" "$HERE/rtsmart/drivers/sys_stat/sys_stat.h" \
   "$A/leakcam/src/"
# the program's version, as the BL616's (version_gen.cmake): firmware/VERSION + the commit (+ -dirty-<UTC> with
# uncommitted changes); in its STATUS frame for the server (DESIGN.md 5.3.10)
V=$(tr -d '[:space:]' < "$HERE/../VERSION")+g$(git -C "$HERE" rev-parse --short=7 HEAD)
[ -z "$(git -C "$HERE" status --porcelain --untracked-files=no)" ] || V=$V-dirty-$(date -u +%Y%m%d%H%M)
printf '#define LEAKCAM_VERSION "%s"\n' "$V" > "$A/leakcam/src/leakcam_version.h"
grep -q 'CONFIG_APP_ENABLE_LEAKCAM' "$A/apps.mk" || echo 'subdirs-$(CONFIG_APP_ENABLE_LEAKCAM) += leakcam' >> "$A/apps.mk"
# test programs a test install left in this SDK tree (tests/firmware/test_install.sh) stay out of the product image
for t in memtest venctest; do
    sed -i "/^subdirs-y += $t\$/d" "$A/apps.mk"; rm -rf "$A/$t"
done
rm -rf "$SDK"/output/k230d_rtos_leakcam_defconfig/images/sdcard/app/*

# The product program in the kernel's ROMFS, /app/leakcam (DESIGN.md 5.3.7, 2026-11-06): inside the A/B slot, so an
# image update and its fallback change kernel and program together. The program's Makefile writes
# app_canmv/leakcam_rom.c (rom_c.py); until the first build an empty one stands in. The root table gets "app".
ROMC=$RTT/app_canmv/leakcam_rom.c
[ -f "$ROMC" ] || python3 "$HERE/../k230_app/rtsmart/rom_c.py" - "$ROMC"
SCON=$RTT/app_canmv/SConscript
grep -q "leakcam_rom.c" "$SCON" || sed -i "s|^src\t+= Glob('config.c')|src\t+= Glob('config.c')\nsrc\t+= Glob('leakcam_rom.c')     # LEAKCAM: the product program in the ROMFS|" "$SCON"
grep -q "leakcam_rom.c" "$SCON" || { echo "app_canmv SConscript: leakcam_rom.c not added"; exit 1; }
grep -q 'LEAKCAM: the program in the ROMFS' "$MNT" || python3 - "$MNT" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'const struct romfs_dirent _root_dirent[] = {\n'
b = ('/* LEAKCAM: the program in the ROMFS, /app/leakcam (leakcam_rom.c, DESIGN.md 5.3.7) */\n'
     'extern const struct romfs_dirent leakcam_rom_app[];\n\n' + a +
     '    {ROMFS_DIRENT_DIR, "app",     (rt_uint8_t *)leakcam_rom_app, 1},\n')
assert s.count(a) == 1, 'mnt.c changed, cannot add the ROMFS program'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: the program in the ROMFS' "$MNT" || { echo "ROMFS program patch not applied"; exit 1; }
# The SDK builds the kernel, then OpenSBI packs it into the slot image, then the apps: the program must exist before
# the kernel that carries it. OpenSBI now waits for leakcam-rom: the apps, then the kernel linked again with them.
MK=$SDK/Makefile
grep -q 'LEAKCAM: leakcam-rom' "$MK" || python3 - "$MK" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = 'opensbi: prepare rtsmart\n'
b = ('# LEAKCAM: leakcam-rom: the apps first (the product program -> the kernel\'s ROMFS source), then the kernel again\n'
     '.PHONY: leakcam-rom\n'
     'leakcam-rom: prepare rtsmart\n'
     '\t@$(MAKE) -C $(SDK_APPS_SRC_DIR) all\n'
     '\t@$(MAKE) -C $(SDK_RTSMART_SRC_DIR) all\n\n'
     'opensbi: prepare rtsmart leakcam-rom\n')
assert s.count(a) == 1, 'SDK Makefile changed, cannot order the ROMFS build'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: leakcam-rom' "$MK" || { echo "SDK Makefile ROMFS order not applied"; exit 1; }

# The USB device on USB-C 1 for the phone's USB setup (DESIGN.md 5.3.8; CherryUSB CDC ACM ttyGS0, 1209:ABD1, tests/
# test043): the SDK's glue calls rt_ioremap without its header: undeclared, the 64-bit address came back as an int
G=$RTT/components/CherryUSB/port/dwc2/usb_glue_kendryte.c
grep -q '<ioremap.h>' "$G" || sed -i 's|#include <rthw.h>|#include <rthw.h>\n#include <ioremap.h>|' "$G"
# Its product string is "CanMV": Android's "open the app for this USB device" dialog showed it (user 2026-11-05:
# LeakCam). String 2 of the CDC descriptor becomes "LeakCam" (bLength 2 + 2 x 7)
D=$RTT/components/canmv_usb/usbd_desc_cdc.h
grep -q 'LEAKCAM: product' "$D" || python3 - "$D" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
a = ("    0x0C, /* bLength */\n    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */\n"
     "    'C',\n    0x00, /* wcChar0 */\n    'a',\n    0x00, /* wcChar1 */\n    'n',\n    0x00, /* wcChar2 */\n"
     "    'M',\n    0x00, /* wcChar3 */\n    'V',\n    0x00, /* wcChar4 */\n")
b = ("    0x10, /* bLength */   /* LEAKCAM: product \"LeakCam\" */\n    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */\n" +
     "".join(f"    '{c}',\n    0x00, /* wcChar{i} */\n" for i, c in enumerate("LeakCam")))
assert s.count(a) == 1, 'usbd_desc_cdc.h changed, cannot rename the USB product'
open(p, 'w').write(s.replace(a, b))
PY
grep -q 'LEAKCAM: product' "$D" || { echo "USB product name patch not applied"; exit 1; }

echo "LEAKCAM board installed into $SDK"
echo "next: cd $SDK && ./leakcam_docker_build.sh k230d_rtos_leakcam_defconfig && ./leakcam_docker_build.sh log   (the defconfig step every time: the build keeps an old .config otherwise)"
