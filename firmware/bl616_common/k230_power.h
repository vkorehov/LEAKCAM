/*
 * The K230's power and its pins, owned by the BL616 (tests/test003, test033). The K230 cannot switch its own rails:
 * every power-off is the BL616's.
 */
#ifndef LEAKCAM_K230_POWER_H
#define LEAKCAM_K230_POWER_H

#include <stdbool.h>
#include <stdint.h>

/* safe off state: reset asserted before K230_PWR is an output, rails off, BOOT1 low, every K230-side pin parked
 * analog (no back-feed into the unpowered K230) */
void k230_power_init(void);
/* rails on with the K230 held in reset, BOOT1 low (NAND boot); the 3V3 ramp in us, 0 = it did not come up (rails off
 * again). The SDIO lines (10 k pull-ups to the K230's 3V3) are read as plain inputs to see it */
uint32_t k230_power_on(void);
/* reset released: the K230 boots */
void k230_power_run(void);
/* reset held, rails off, discharged (300 ms settle), the pins parked again; the 3V3 fall in us, 0 = it stayed up */
uint32_t k230_power_off(void);
bool k230_power_is_on(void);
/* when the K230 was last switched off, ms since boot (0 = not in this boot): the board cools from then (climate.c) */
uint64_t k230_power_off_ms(void);
/* K230_ALIVE (IO01 <- K230 GPIO2): the program's heartbeat; reads low with the K230 off (its pull-up is on the 3V3) */
bool k230_alive(void);
/* shell `k230 usbboot | off` (both product firmwares, user 2026-11-05: flash the K230 without the test firmware):
 * usbboot = rails on, BOOT1 high, reset released: the K230's ROM waits on USB-C 1 for k230_flash
 * (firmware/k230_board/flash_nand.sh); off = everything off, BOOT1 low again. Refused while the K230 is on */

#endif
