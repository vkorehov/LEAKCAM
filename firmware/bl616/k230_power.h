/*
 * The K230's power and its pins, owned by the BL616 (tests/test003, test033).
 */
#ifndef LEAKCAM_K230_POWER_H
#define LEAKCAM_K230_POWER_H

/* safe off state: reset asserted before K230_PWR is an output, rails off, BOOT1 low, every K230-side pin parked
 * analog (no back-feed into the unpowered K230) */
void k230_power_init(void);

#endif
