/*
 * Retained event log (DESIGN.md 5.2, user 2026-11-08: "keep some debug log even in product"): the last 32 things the
 * BL616 decided or saw, in HBN RAM through sleeps and software resets, so what happened while nobody watched (on the
 * cell the console is gone with USB-C 1) can be read afterwards: `evlog` on the console. Lost with 3V3_SLEEP (a power
 * loss reads as empty). Both firmwares log into the same record (.hbn_noinit sorted by name: one address).
 */
#ifndef LEAKCAM_EVLOG_H
#define LEAKCAM_EVLOG_H

#include <stdint.h>

enum ev {
    EV_BOOT = 1,        /* arg = 0 full, 1 setup firmware; val = GLB reset bits (rst in metrics) */
    EV_USB,             /* arg = 1 plugged, 0 unplugged */
    EV_SLEEP,           /* arg = 1 the low-power sleep begins, 0 ends (val = its BLE wakes) */
    EV_K230,            /* arg = 1 the K230 session starts, 0 stops */
    EV_ADV,             /* BLE advertising; arg = 1 on, 0 off; val = interval ms */
    EV_SWITCH,          /* to the other firmware; arg = 0 full, 1 setup */
    EV_MARK,            /* a step of a path being traced; arg = the step, val = a value */
    EV_WEDGE,           /* the BLE core wedge (skill bl616-ble-low-power 5): arg = 1 parked (asleep, advertising per the
                         * host, no BLE wake in WEDGE_CHECK_MS; val = the wakes of any kind in it), 2 the BLE start not
                         * done in BLE_START_DEADLINE_MS (the init spin; val = 0). Only the EN pin or a power loss clears
                         * it */
    EV_REG,             /* a register dump (bl616_setup/blecore.h): arg = point * 8 + register, val = its value */
};

void evlog_add(enum ev ev, int arg, unsigned val);
/* oldest first; also the shell command `evlog` (`evlog clear`) */
void evlog_print(void);

#endif
