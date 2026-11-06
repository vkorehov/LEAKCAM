#include "battery.h"

#include <stdint.h>
#include <stdio.h>

#include "shell.h"

#define MAGIC 0x42415431u                    /* "BAT1": the record holds a reading */

/* at a fixed place in both firmwares: .hbn_noinit sections are sorted by name (bl616_leakcam.ld.in) */
static struct {
    uint32_t magic;
    int32_t mv;
} rec __attribute__((section(".hbn_noinit.battery")));

void battery_seen(int mv)
{
    if (mv < 0 || battery_usb_power())
        return;                              /* the ADC failed, or it read the charger's rail (tests/test023) */
    rec.mv = mv;
    rec.magic = MAGIC;
}

int battery_mv(void)
{
    return rec.magic == MAGIC ? (int)rec.mv : -1;
}

static void cmd_battery(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("battery: %d mV (last on battery, the K230's reading), %s\r\n", battery_mv(),
           battery_usb_power() ? "charging (USB power)" : "on battery");
}
SHELL_CMD_EXPORT_ALIAS(cmd_battery, battery, the battery: last K230 reading and USB power);
