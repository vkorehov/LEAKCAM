#include "thresholds.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "easyflash.h"
#include "shell.h"

#define ENV_KEY "thr"

/* in flash as one blob: both written together, at most once per change (a check-in every hour must not wear it) */
struct thr {
    uint16_t probe_mv;
    uint16_t humid;
};

static struct thr cur = { THR_PROBE_MV, THR_HUMID };
static bool stored;              /* cur came from flash (the server's), not the defaults */
static unsigned writes;          /* flash writes this boot: one per change, none for the same values (tests/test142) */

static bool probe_ok(long v)
{
    return v >= THR_PROBE_MV_MIN && v <= THR_PROBE_MV_MAX && v % THR_PROBE_STEP == 0;
}

static bool humid_ok(long v)
{
    return v >= THR_HUMID_MIN && v <= THR_HUMID_MAX;
}

void thresholds_load(void)
{
    struct thr t;
    if (ef_get_env_blob(ENV_KEY, &t, sizeof(t), NULL) != sizeof(t))
        return;                  /* never set: the defaults */
    if (probe_ok(t.probe_mv))
        cur.probe_mv = t.probe_mv;
    if (humid_ok(t.humid))
        cur.humid = t.humid;
    stored = true;
}

int thresholds_probe_mv(void)
{
    return cur.probe_mv;
}

int thresholds_humid(void)
{
    return cur.humid;
}

bool thresholds_set(long probe_mv, long humid)
{
    struct thr t = cur;
    if (probe_mv >= 0) {
        if (probe_ok(probe_mv))
            t.probe_mv = (uint16_t)probe_mv;
        else
            printf("thresholds: probe %ld mV from the server ignored (%d-%d mV, steps of %d)\r\n", probe_mv,
                   THR_PROBE_MV_MIN, THR_PROBE_MV_MAX, THR_PROBE_STEP);
    }
    if (humid >= 0) {
        if (humid_ok(humid))
            t.humid = (uint16_t)humid;
        else
            printf("thresholds: humidity %ld %%RH from the server ignored (%d-%d)\r\n", humid, THR_HUMID_MIN,
                   THR_HUMID_MAX);
    }
    if (stored && !memcmp(&t, &cur, sizeof(t)))
        return false;            /* unchanged: no flash write */
    bool probe_changed = t.probe_mv != cur.probe_mv;
    if (ef_set_env_blob(ENV_KEY, &t, sizeof(t)) != EF_NO_ERR) {
        printf("thresholds: flash write failed, kept %d mV %d %%RH\r\n", cur.probe_mv, cur.humid);
        return false;            /* the next check-in tries again */
    }
    printf("thresholds: probe %d -> %d mV, humidity %d -> %d %%RH, stored\r\n", cur.probe_mv, t.probe_mv, cur.humid,
           t.humid);
    cur = t;
    stored = true;
    writes++;
    return probe_changed;
}

/* `thresholds`: the values in use and where they came from */
static void cmd_thresholds(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("thresholds: probe %d mV, humidity %d %%RH, %s, flash writes %u this boot\r\n", cur.probe_mv, cur.humid,
           stored ? "from the server (flash)" : "defaults", writes);
}
SHELL_CMD_EXPORT_ALIAS(cmd_thresholds, thresholds, leak thresholds: probe mV and humidity alarm);
