#include "leak_check.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_acomp.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "bl616_common.h"   /* ENABLE */
#include "shell.h"

#include "board_pins.h"
#include "k230_session.h"
#include "metrics.h"
#include "presence.h"
#include "usb_full.h"
#include "usb_power.h"

/* the reference is vio_sel x 50 mV, the scaling factor has no effect: 22 = 1.10 V, between a pressed wet towel
 * (0.95 V) and a dried salt film (1.17 V) (tests/test034, 2026-10-31); dry the node sits at 1.65 V */
#define VIO_LEAK      22
#define POLL_MS       100
#define WET_POLLS     2         /* wet this many polls in a row: a probe edge in a film of water chatters */
#define CHECK_S       86400     /* until the server says otherwise (DESIGN.md 5.3.12) */
#define CHECK_MIN_S   60
#define CHECK_MAX_S   (7 * 86400)
#define JOB_MAX_MS    60000     /* on battery: no DONE by then = the check failed, the K230 off */

static volatile uint32_t check_s = CHECK_S;
static volatile bool busy;
static uint64_t busy_since, next_at;
static unsigned checks, wet_edges;
static char last[24];

static bool wet(void)
{
    return bflb_acomp_get_result(AON_ACOMP1_ID) == 0;   /* 1 while the probe node is above the reference */
}

static void probe_init(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(gpio, PIN_LEAK_SENS, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC0,                    /* GPIO20 */
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = VIO_LEAK,
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,                /* slowest, lowest current */
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
    };
    bflb_acomp_init(AON_ACOMP1_ID, &cfg);
    bflb_acomp_enable(AON_ACOMP1_ID);
    bflb_mtimer_delay_ms(1);                                    /* settling, slow bias */
}

/* a K230 leak check for `why`: a session with that reason, or CHECK to the K230 already on */
static void request(const char *why)
{
    busy = true;
    busy_since = bflb_mtimer_get_time_ms();
    checks++;
    if (k230_session_active()) {
        printf("leak: check %s: CHECK to the running K230\r\n", why);
        k230_session_check(why);
    } else {
        printf("leak: check %s: K230 session\r\n", why);
        k230_session_start(why, usb_full_cmd);   /* the cable's commands too, should a phone be plugged meanwhile */
    }
}

void leak_check_done(const struct link_msg *m)
{
    snprintf(last, sizeof(last), "%s", m->args);
    bool changed = strstr(m->args, "changed") != NULL, w = wet();
    printf("leak: DONE %s %lu ms after the ask, probe %s\r\n", m->args,
           busy ? (unsigned long)(bflb_mtimer_get_time_ms() - busy_since) : 0UL, w ? "wet" : "dry");
    metrics_job(m->args);
    busy = false;
    if (changed || w)
        presence_now();                          /* the server sees it at once, with the job in the check-in */
    if (!usb_present())
        k230_session_stop("leak check done");   /* on battery the K230 only for the check (DESIGN.md 5.3.12) */
}

bool leak_check_busy(void)
{
    return busy;
}

void leak_check_every(uint32_t s)
{
    if (s < CHECK_MIN_S || s > CHECK_MAX_S || s == check_s)
        return;
    printf("leak: check every %lu s (server)\r\n", (unsigned long)s);
    check_s = s;
    metrics_intervals_set(0, s);             /* kept across a reset */
    next_at = bflb_mtimer_get_time_ms() + (uint64_t)s * 1000;
}

static void watch_task(void *arg)
{
    (void)arg;
    unsigned wet_n = 0;
    bool was_wet = wet();
    /* the check at the start: on battery now; on USB the usb session's own first check (the K230's job checks at its
     * start) */
    next_at = bflb_mtimer_get_time_ms() + (usb_present() ? (uint64_t)check_s * 1000 : 0);
    for (;;) {
        uint64_t now = bflb_mtimer_get_time_ms();
        bool w = wet();
        wet_n = w ? wet_n + 1 : 0;
        if (!was_wet && wet_n >= WET_POLLS) {
            was_wet = true;
            wet_edges++;
            printf("leak: probe wet\r\n");
            presence_now();                      /* the probe alone is reported at once (probe= in the check-in) */
            if (!busy)
                request("leak");
        } else if (was_wet && !w) {
            was_wet = false;
            printf("leak: probe dry\r\n");
        }
        if (now >= next_at) {
            next_at = now + (uint64_t)check_s * 1000;
            if (!busy)
                request("rtc");
        }
        if (busy && !usb_present() && now - busy_since > JOB_MAX_MS) {
            printf("leak: no DONE %u s after the ask: the check failed\r\n", JOB_MAX_MS / 1000);
            busy = false;
            k230_session_stop("leak check: no DONE");
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void leak_check_start(void)
{
    uint32_t unused, kept;
    metrics_intervals(&unused, &kept);
    if (kept >= CHECK_MIN_S && kept <= CHECK_MAX_S)
        check_s = kept;                      /* the server's, from before the reset */
    probe_init();
    if (xTaskCreate(watch_task, "leak", 1024, NULL, 4, NULL) != pdPASS)
        vAssertCalled();
}

/* `leak`: the state; `leak check`: a check now (as the RTC's) */
static void cmd_leak(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "check")) {
        if (busy)
            printf("leak: a check is running\r\n");
        else
            request("rtc");
        return;
    }
    uint64_t now = bflb_mtimer_get_time_ms();
    printf("leak: probe %s, every %lu s, next in %lu s, checks %u, wet edges %u, last %s%s\r\n", wet() ? "wet" : "dry",
           (unsigned long)check_s, (unsigned long)(next_at > now ? (next_at - now) / 1000 : 0), checks, wet_edges,
           last[0] ? last : "none", busy ? ", one running" : "");
}
SHELL_CMD_EXPORT_ALIAS(cmd_leak, leak, leak checks: probe and last result [check]);
