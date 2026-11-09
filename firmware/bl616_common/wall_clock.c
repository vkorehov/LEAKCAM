#include "wall_clock.h"

#include <stdio.h>
#include <stdlib.h>

#include "bflb_mtimer.h"
#include "shell.h"

static uint64_t base_s, base_ms;             /* the server's time and the timer when it came; base_s 0 = none */
static unsigned sets;

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
    sets++;
}

uint32_t wall_clock_now(void)
{
    return base_s ? (uint32_t)(base_s + (bflb_mtimer_get_time_ms() - base_ms) / 1000) : 0;
}

/* `clock`: the clock and how often it was set; `clock <unix s>`: set it as given, past the 5 s rule (tests/test138
 * moves it to see both chips' rule: the K230 follows a BL616 frame beyond 5 s, the server's next answer sets it back) */
static void cmd_clock(int argc, char **argv)
{
    if (argc > 1) {
        base_ms = bflb_mtimer_get_time_ms();
        base_s = strtoul(argv[1], NULL, 10);
        printf("clock: %lu set by hand\r\n", (unsigned long)base_s);
        return;
    }
    printf("clock: %lu (set %u times)\r\n", (unsigned long)wall_clock_now(), sets);
}
SHELL_CMD_EXPORT_ALIAS(cmd_clock, clock, the wall clock from the server X-Time [unix s: set it]);
