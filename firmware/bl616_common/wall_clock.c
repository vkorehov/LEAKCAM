#include "wall_clock.h"

#include <stdio.h>
#include <stdlib.h>

#include "shell.h"

#include "rtc_clock.h"

#define MAGIC 0x57434c4bu                    /* "WCLK" */

/* the server's time and the RTC second it came at: kept through HBN sleep, resets and the firmware switch (both
 * firmwares at the same address, .hbn_noinit sorted by name); lost only with the battery (and the RTC with it) */
static struct {
    uint32_t magic, unix_s, rtc_s;
} base __attribute__((section(".hbn_noinit.wclock")));
static unsigned sets;

static void set(uint32_t unix_s)
{
    base.unix_s = unix_s;
    base.rtc_s = rtc_clock_s();
    base.magic = MAGIC;
}

#define DRIFT_S 5                            /* changed only beyond this (user, 2026-11-07, DESIGN.md 5.3.12) */

void wall_clock_set(uint32_t unix_s)
{
    uint32_t now = wall_clock_now();
    if (now && (int32_t)(unix_s - now) <= DRIFT_S && (int32_t)(now - unix_s) <= DRIFT_S)
        return;
    printf("clock: %lu from the server (was %ld s off)\r\n", (unsigned long)unix_s,
           now ? (long)((int32_t)(now - unix_s)) : 0L);
    set(unix_s);
    sets++;
}

uint32_t wall_clock_now(void)
{
    return base.magic == MAGIC && base.unix_s ? base.unix_s + (rtc_clock_s() - base.rtc_s) : 0;
}

/* `clock`: the clock and how often it was set; `clock <unix s>`: set it as given, past the 5 s rule (tests/test138
 * moves it to see both chips' rule: the K230 follows a BL616 frame beyond 5 s, the server's next answer sets it back) */
static void cmd_clock(int argc, char **argv)
{
    if (argc > 1) {
        set((uint32_t)strtoul(argv[1], NULL, 10));
        printf("clock: %lu set by hand\r\n", (unsigned long)base.unix_s);
        return;
    }
    printf("clock: %lu (set %u times)\r\n", (unsigned long)wall_clock_now(), sets);
}
SHELL_CMD_EXPORT_ALIAS(cmd_clock, clock, the wall clock from the server X-Time [unix s: set it]);
