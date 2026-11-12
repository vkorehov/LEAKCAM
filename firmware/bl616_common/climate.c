#include "climate.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "shell.h"

#include "k230_power.h"
#include "metrics.h"
#include "rtc_clock.h"
#include "thresholds.h"

#define READ_S     600           /* the humidity reading every 10 min, as before 2026-11-06 */
#define COOL_S     (3 * 3600)    /* the room only after the board was cool this long (user 2026-11-10) */
#define COOL_MIN_S 10            /* `climate quiet`'s shortest (tests) */

#define MAGIC      0x434c494du   /* "CLIM" */

/* in HBN RAM (both firmwares at the same address): the window and the next reading through HBN sleep (the full
 * firmware's wake reads first thing when it is due); RTC seconds */
static struct {
    uint32_t magic;
    uint32_t heat_s;             /* the last heat seen: a reading while warm, the unplug, the K230's power-off, a boot */
    uint32_t next_s;             /* the next reading due */
    uint32_t cool_s;             /* the window: COOL_S, `climate quiet` on the bench (until the next reset) */
} st __attribute__((section(".hbn_noinit.climate")));
static bool (*warm)(void);
static void (*alarm)(int rh_x10);
static uint64_t k230_off_seen;   /* k230_power_off_ms() taken into heat_s */
static unsigned reads, fails, rooms, alarms;
static int last_rh = -1, last_t;
static TaskHandle_t task;

/* s the board has been cool: 0 while warm; from the latest heat end (the K230's power-off, the unplug, a boot) */
static uint32_t cool_now(void)
{
    uint32_t now = rtc_clock_s();
    uint64_t off = k230_power_off_ms();
    if (off != k230_off_seen) {          /* a power-off on the core timer (k230_power.c): its RTC second */
        k230_off_seen = off;
        uint32_t ago = (uint32_t)((bflb_mtimer_get_time_ms() - off) / 1000);
        if ((int32_t)(now - ago - st.heat_s) > 0)
            st.heat_s = now - ago;
    }
    if (warm()) {
        st.heat_s = now;
        return 0;
    }
    return now - st.heat_s;
}

static uint32_t quiet_s(void)
{
    return cool_now();
}

void climate_heat(void)
{
    st.heat_s = rtc_clock_s();
}

void climate_read(const char *why)
{
    bool hot = warm();
    uint32_t q = cool_now();
    int rh, t;
    if (!metrics_aht20(&rh, &t)) {
        fails++;
        printf("climate: %s: AHT20 read failed\r\n", why);
        return;
    }
    reads++;
    last_rh = rh;
    last_t = t;
    bool room = !hot && q >= st.cool_s;
    if (room) {
        rooms++;
        metrics_room(t, rh);
    }
    printf("climate: %s: %s%d.%d C %d.%d %%RH, %s %lu s%s\r\n", why, t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10,
           rh / 10, rh % 10, hot ? "warm" : "cool", (unsigned long)q, room ? ": the room" : "");
    if (rh >= thresholds_humid() * 10) {     /* the server's, 85 %RH by default (thresholds.h) */
        alarms++;
        printf("climate: humidity %d.%d %%RH >= %d: alarm\r\n", rh / 10, rh % 10, thresholds_humid());
        alarm(rh);
    }
}

static void climate_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t now = rtc_clock_s();
        if ((int32_t)(st.next_s - now) <= 0) {
            st.next_s = now + READ_S;
            climate_read("timer");
            continue;
        }
        /* until the next reading, or at once on `climate now`: nothing wakes the chip in between */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((st.next_s - now) * 1000)))
            climate_read("now");
    }
}

uint32_t climate_due_s(void)
{
    return st.next_s;
}

void climate_start(bool (*warm_fn)(void), void (*alarm_fn)(int rh_x10), bool hbn_wake)
{
    warm = warm_fn;
    alarm = alarm_fn;
    k230_off_seen = k230_power_off_ms();
    if (st.magic != MAGIC || !hbn_wake) {    /* a reset or a switch: whatever ran before warmed the board, now */
        st.heat_s = st.next_s = rtc_clock_s();  /* the first reading at once */
        st.cool_s = COOL_S;
        st.magic = MAGIC;
    }
    metrics_quiet(quiet_s);
    if (xTaskCreate(climate_task, "climate", 768, NULL, 4, &task) != pdPASS)
        vAssertCalled();
}

/* `climate`: the state; `climate now`: a reading now; `climate quiet <s>`: the cool window until the next reset;
 * `climate sim <rh %>|off`: the humidity every reading gives (tests, hands-free: test140) */
static void cmd_climate(int argc, char **argv)
{
    if (!task) {
        printf("climate: not started\r\n");
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "now")) {
        xTaskNotifyGive(task);
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "quiet")) {
        long s = strtol(argv[2], NULL, 10);
        if (s < COOL_MIN_S || s > 7 * 86400) {
            printf("climate: quiet %d-%d s\r\n", COOL_MIN_S, 7 * 86400);
            return;
        }
        st.cool_s = (uint32_t)s;
    } else if (argc > 2 && !strcmp(argv[1], "sim")) {
        int x10 = strcmp(argv[2], "off") ? (int)(strtol(argv[2], NULL, 10) * 10) : -1;
        if (x10 > 1000) {
            printf("climate: sim 0-100 %%RH or off\r\n");
            return;
        }
        metrics_rh_sim(x10);
        printf("climate: humidity %s\r\n", x10 < 0 ? "from the sensor" : "simulated");
    }
    bool hot = warm();
    uint32_t q = cool_now();
    printf("climate: last %s%d.%d C %d.%d %%RH, %s %lu s (room after %lu s), reads %u, fails %u, rooms %u, alarms %u\r\n",
           last_t < 0 ? "-" : "", abs(last_t) / 10, abs(last_t) % 10, last_rh < 0 ? 0 : last_rh / 10,
           last_rh < 0 ? 0 : last_rh % 10, hot ? "warm" : "cool", (unsigned long)q, (unsigned long)st.cool_s, reads, fails,
           rooms, alarms);
}
SHELL_CMD_EXPORT_ALIAS(cmd_climate, climate, temperature and humidity [now | quiet <s> | sim <rh %>|off]);
