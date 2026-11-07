#include "wall_clock.h"

#include <stdio.h>

#include "bflb_mtimer.h"

static uint64_t base_s, base_ms;             /* the server's time and the timer when it came; base_s 0 = none */

#define DRIFT_S 5                            /* changed only beyond this (user, 2026-11-07, DESIGN.md 5.3.12) */

void wall_clock_set(uint32_t unix_s)
{
    uint32_t now = wall_clock_now();
    if (now && (int32_t)(unix_s - now) <= DRIFT_S && (int32_t)(now - unix_s) <= DRIFT_S)
        return;
    printf("clock: %lu from the server (was %ld s off)\r\n", (unsigned long)unix_s,
           now ? (long)((int32_t)(now - unix_s)) : 0L);
    base_ms = bflb_mtimer_get_time_ms();
    base_s = unix_s;
}

uint32_t wall_clock_now(void)
{
    return base_s ? (uint32_t)(base_s + (bflb_mtimer_get_time_ms() - base_ms) / 1000) : 0;
}
