#include "blecore.h"

#include <stdio.h>

#include "bl616_pds.h"
#include "shell.h"

#include "evlog.h"

/* the order is evlog.c's reg_names */
static const struct {
    const char *name;
    uintptr_t addr;
    int core;               /* in the BLE core: not read at BLECORE_BOOT */
} regs[] = {
    { "PDS_CTL", PDS_BASE + PDS_CTL_OFFSET, 0 },
    { "PDS_CTL4", PDS_BASE + PDS_CTL4_OFFSET, 0 },
    { "PDS_CTL5", PDS_BASE + PDS_CTL5_OFFSET, 0 },
    { "DEEPSLCNTL", 0x28000030u, 1 },
    { "BLE_0x100", 0x28000100u, 1 },    /* bit 31: the native clock sample, never cleared when wedged */
};
#define NREGS (sizeof(regs) / sizeof(regs[0]))

static unsigned rd(unsigned i)
{
    return (unsigned)*(volatile uint32_t *)regs[i].addr;
}

void blecore_dump(enum blecore_point point)
{
    for (unsigned i = 0; i < NREGS; i++)
        if (point != BLECORE_BOOT || !regs[i].core)
            evlog_add(EV_REG, (int)(point * 8 + i), rd(i));
}

static void cmd_blecore(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (unsigned i = 0; i < NREGS; i++)
        printf("blecore: %-10s %08x\r\n", regs[i].name, rd(i));
}
SHELL_CMD_EXPORT_ALIAS(cmd_blecore, blecore, the BLE core wedge registers now (evlog has them at boot and sleeps));
