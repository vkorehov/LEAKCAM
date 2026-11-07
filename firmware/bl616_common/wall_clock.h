/*
 * The wall clock (DESIGN.md 5.3.12): no NTP, the server's check-in answer carries the time ("time": unix s) and the full
 * firmware sets it here; the K230's WAKE carries it (k230_link.h, 0 = not known). Kept against the BL616's timer for
 * now: lost with a reset or the switch to the other firmware until the RTC keeps it through HBN (the sleep, next).
 */
#ifndef LEAKCAM_WALL_CLOCK_H
#define LEAKCAM_WALL_CLOCK_H

#include <stdint.h>

/* changed only when it is more than 5 s off (or not set yet): every exchange tunes it, a small drift is left */
void wall_clock_set(uint32_t unix_s);
/* unix seconds now, 0 = not known yet */
uint32_t wall_clock_now(void);

#endif
