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
#include "capture.h"
#include "hbn_sleep.h"
#include "k230_session.h"
#include "metrics.h"
#include "presence.h"
#include "rtc_clock.h"
#include "thresholds.h"
#include "usb_full.h"
#include "usb_power.h"

/* the reference is vio_sel x 50 mV, the scaling factor has no effect (tests/test034): the server's probe threshold,
 * 1100 mV by default (thresholds.h); dry the node sits at 1.65 V */
#define POLL_MS       100
#define WET_POLLS     2         /* wet this many polls in a row: a probe edge in a film of water chatters */
#define CHECK_S       86400     /* until the server says otherwise (DESIGN.md 5.3.12) */
#define CHECK_MIN_S   10        /* the bench's shortest (tests run at it: test108); a check takes a few s */
#define CHECK_MAX_S   (7 * 86400)
#define JOB_MAX_MS    60000     /* on battery: no DONE by then = the check failed, the K230 off */

static volatile uint32_t check_s = CHECK_S;
static volatile bool busy;
static uint64_t busy_since;                /* core timer ms: within this boot (a check never spans a sleep) */
static unsigned checks, wet_edges;
static char last[48];                    /* "unfocused,unfocused,100,100" and room */
static int armed_mv;                       /* the comparator's reference as armed (`leak`, tests/test142) */

bool leak_check_wet(void)
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
        .vio_sel = (uint8_t)(thresholds_probe_mv() / THR_PROBE_STEP),   /* a multiple of the step (thresholds.c) */
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,                /* slowest, lowest current */
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
    };
    bflb_acomp_init(AON_ACOMP1_ID, &cfg);
    bflb_acomp_enable(AON_ACOMP1_ID);
    bflb_mtimer_delay_ms(1);                                    /* settling, slow bias */
    armed_mv = thresholds_probe_mv();
}

void leak_check_probe(void)
{
    probe_init();
    printf("leak: probe reference %d mV\r\n", thresholds_probe_mv());
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
    bool changed = strstr(m->args, "changed") != NULL, w = leak_check_wet();
    printf("leak: DONE %s %lu ms after the ask, probe %s\r\n", m->args,
           busy ? (unsigned long)(bflb_mtimer_get_time_ms() - busy_since) : 0UL, w ? "wet" : "dry");
    metrics_job(m->args);
    busy = false;
    if (changed || w)
        presence_now();                          /* the server sees it at once, with the job in the check-in */
    /* on battery the K230 only for the check (DESIGN.md 5.3.12); kept on for pictures being taken, and after a change
     * or a wet probe for the check-in's answer, whose leak record it takes at once (capture.c ends it otherwise) */
    if (!usb_present() && !capture_busy() && !changed && !w)
        k230_session_stop("leak check done");
}

bool leak_check_busy(void)
{
    return busy;
}

void leak_check_now(const char *why)
{
    if (busy)
        printf("leak: check %s: one running %lu ms already, its DONE answers it\r\n", why,
               (unsigned long)(bflb_mtimer_get_time_ms() - busy_since));
    else
        request(why);
}

void leak_check_every(uint32_t s)
{
    if (s < CHECK_MIN_S || s > CHECK_MAX_S || s == check_s)
        return;
    printf("leak: check every %lu s (server)\r\n", (unsigned long)s);
    check_s = s;
    metrics_intervals_set(0, s);             /* kept across a reset */
    hbn_sleep_leak_set(rtc_clock_s() + s);
}

static void watch_task(void *arg)
{
    (void)arg;
    unsigned wet_n = 0;
    /* a wake from HBN: the due check and the probe's state kept through the sleep (a probe gone wet meanwhile woke it:
     * its edge below). A reset: the check at the start on battery now; on USB the usb session's own first check (the
     * K230's job checks at its start) */
    bool was_wet = hbn_sleep_woke() ? hbn_sleep_wet() : leak_check_wet();
    if (!hbn_sleep_woke())
        hbn_sleep_leak_set(rtc_clock_s() + (usb_present() ? check_s : 0));
    hbn_sleep_wet_set(was_wet);
    for (;;) {
        uint32_t now = rtc_clock_s();
        bool w = leak_check_wet();
        wet_n = w ? wet_n + 1 : 0;
        if (!was_wet && wet_n >= WET_POLLS) {
            was_wet = true;
            hbn_sleep_wet_set(true);
            wet_edges++;
            printf("leak: probe wet\r\n");
            presence_now();                      /* the probe alone is reported at once (probe= in the check-in) */
            if (!busy)
                request("leak");
        } else if (was_wet && !w) {
            was_wet = false;
            hbn_sleep_wet_set(false);
            printf("leak: probe dry\r\n");
        }
        if ((int32_t)(now - hbn_sleep_leak_due()) >= 0) {
            hbn_sleep_leak_set(now + check_s);
            if (!busy)
                request("rtc");
        }
        if (busy && !usb_present() && bflb_mtimer_get_time_ms() - busy_since > JOB_MAX_MS) {
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
    int32_t next = (int32_t)(hbn_sleep_leak_due() - rtc_clock_s());
    printf("leak: probe %s, every %lu s, next in %lu s, checks %u, wet edges %u, last %s%s, reference %d mV\r\n",
           leak_check_wet() ? "wet" : "dry", (unsigned long)check_s, (unsigned long)(next > 0 ? next : 0), checks,
           wet_edges, last[0] ? last : "none", busy ? ", one running" : "", armed_mv);
}
SHELL_CMD_EXPORT_ALIAS(cmd_leak, leak, leak checks: probe and last result [check]);
