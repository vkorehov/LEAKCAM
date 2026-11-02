#include "slot.h"

#include <stdio.h>
#include "bflb_boot2.h"
#include "bflb_mtimer.h"
#include "usbd_core.h"
#include "crash_rec.h"

extern int bl_sys_reset_por(void);

/* bflb_boot2_init() reads the partition table into RAM; again here, so `slot` works before the firmware's start ran it */
int slot_active(void)
{
    bflb_partition_config_t fw;
    if (bflb_boot2_init() != 0 || bflb_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &fw) != 0)
        return -1;
    return fw.activeIndex;
}

/* boot2 picks the partition table with the higher age; bflb_boot2_update_ptable() flips the FW entry's active index
 * and writes it to the other table with age + 1 (one 4 KB erase + write). Then the same reset as `isp`: USB detached
 * first and 500 ms before the reset, so the host sees the next firmware as a new attach (wdg_guard.c cmd_isp). */
int slot_switch(void)
{
    bflb_partition_config_t fw;
    if (bflb_boot2_init() != 0 || bflb_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &fw) != 0)
        return -1;
    int to = !fw.activeIndex;
    if (bflb_boot2_update_ptable(&fw) != 0)
        return -1;
    printf("slot: %d -> %d, reset in 0.5 s\r\n", !to, to);
    bflb_mtimer_delay_ms(20);
    (void)usbd_deinitialize(0);
    bflb_mtimer_delay_ms(500);
    (void)bl_sys_reset_por();                /* never returns */
    return -1;
}

#ifdef CONFIG_SHELL
#include <string.h>
#include "shell.h"

static void cmd_slot(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "switch") == 0) {
        if (slot_switch() != 0)
            printf("slot: partition table write failed\r\n");
        return;
    }
    bflb_partition_config_t fw;
    if (bflb_boot2_init() != 0 || bflb_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &fw) != 0) {
        printf("slot: no partition table\r\n");
        return;
    }
    printf("slot: %s\r\n", crash_rec_build);
    printf("slot: active %u (0x%lx), age %lu, other 0x%lx\r\n", fw.activeIndex,
           (unsigned long)fw.Address[fw.activeIndex], (unsigned long)fw.age, (unsigned long)fw.Address[!fw.activeIndex]);
}
SHELL_CMD_EXPORT_ALIAS(cmd_slot, slot, the firmware slot: show or switch);
#endif
