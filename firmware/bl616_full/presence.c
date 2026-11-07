#include "presence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "easyflash.h"
#include "shell.h"

#include "api.h"
#include "device_id.h"
#include "leak_check.h"
#include "metrics.h"
#include "ota_fetch.h"
#include "switch_to_setup.h"
#include "wifi_check.h"

#define CHECKIN_S      3600                     /* until the server says otherwise (DESIGN.md 5.3.12: 60 min) */
#define CHECKIN_MIN_S  5
#define CHECKIN_MAX_S  3600
#define MISSED_MAX  3                        /* presence lost after this many failed check-ins in a row */

static uint32_t checkin_s = CHECKIN_S;
static enum wifi_check_state last = WCHK_IDLE;
static uint16_t last_detail;
static uint32_t last_ms;
static unsigned missed;                      /* failed check-ins in a row */
static TaskHandle_t task;

static void no_progress(enum wifi_check_state st, uint16_t detail)
{
    (void)st;
    (void)detail;
}

/* a number in the server's JSON answer, "<key>":<n>; -1 when it is not there */
static long answer_value(const char *body, const char *key)
{
    char k[24];
    snprintf(k, sizeof(k), "\"%s\":", key);
    const char *p = strstr(body, k);
    return p ? strtol(p + strlen(k), NULL, 10) : -1;
}

static void check_in(void)
{
    char ssid[33], psk[64], path[API_PATH_CAP], body[384];   /* an upgrade offer ~280 B */
    size_t ss = 0, ps = 0;
    size_t sl = ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid) - 1, &ss);
    size_t pl = ef_get_env_blob("wifi_psk", psk, sizeof(psk) - 1, &ps);
    if (ss == 0) {
        switch_to_setup(NULL);            /* erased (`provision erase`): a phone sets it there */
        return;
    }
    /* the network it is on, for the app (an SSID is not secret; the passphrase never leaves the board) */
    struct wifi_check_target t;
    api_poll_target(&t, path, ssid, sl, body, sizeof(body));
    last = wifi_check_run(ssid, sl, psk, ps ? pl : 0, &t, NULL, no_progress, &last_detail);
    last_ms = (uint32_t)bflb_mtimer_get_time_ms();
    /* the server drives both intervals (DESIGN.md 5.3.12): "checkin", "leakcheck"; the clock comes with every answer's
     * X-Time header (wifi_check.c) */
    long v = last == WCHK_INTERNET ? answer_value(body, "checkin") : -1;
    if (v >= CHECKIN_MIN_S && v <= CHECKIN_MAX_S) {
        checkin_s = (uint32_t)v;
        metrics_intervals_set(checkin_s, 0);   /* kept across a reset */
    }
    if ((v = last == WCHK_INTERNET ? answer_value(body, "leakcheck") : -1) > 0)
        leak_check_every((uint32_t)v);

    if (last == WCHK_CAPTIVE && last_detail == 410) {
        switch_to_setup("unused");        /* no user left: back to setup (DESIGN.md 5.3.5, tests/test111) */
        return;
    }
    if (last != WCHK_INTERNET) {
        /* presence does not flap: lost only after MISSED_MAX failed check-ins in a row, a short outage never starts
         * the setup firmware (DESIGN.md 5.3.6) */
        if (++missed >= MISSED_MAX)
            switch_to_setup("lost");      /* presence lost, for whatever reason (tests/test117) */
        return;
    }
    missed = 0;
    if (ota_fetch(body, ssid, sl, psk, ps ? pl : 0)) {
        switch_to_setup("ota");           /* the copy is in: the setup firmware unpacks it (DESIGN.md 5.3.7) */
        return;
    }
    if (strstr(body, "\"ble\":1"))
        switch_to_setup("wake");          /* a phone out of range asked through the server (tests/test119) */
}

static void presence_task(void *arg)
{
    (void)arg;
    uint32_t kept, unused;
    metrics_intervals(&kept, &unused);
    if (kept >= CHECKIN_MIN_S && kept <= CHECKIN_MAX_S)
        checkin_s = kept;                    /* the server's, from before the reset */
    vTaskDelay(pdMS_TO_TICKS(5000));         /* online within seconds of the start */
    for (;;) {
        check_in();                        /* on USB and on battery; it needs a stored network */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(checkin_s * 1000));   /* presence_now() ends the wait */
    }
}

void presence_start(void)
{
    if (xTaskCreate(presence_task, "pres", 2048, NULL, 2, &task) != pdPASS)
        vAssertCalled();
}

void presence_now(void)
{
    if (task)
        xTaskNotifyGive(task);
}

static void cmd_presence(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("presence: id %s, server %s:%s, check-in every %lu s\r\n", device_id(), API_HOST, API_PORT,
           (unsigned long)checkin_s);
    if (last_ms)
        printf("presence: last check-in state 0x%02x detail %u, %lu s ago, missed %u of %u\r\n", last, last_detail,
               (unsigned long)(((uint32_t)bflb_mtimer_get_time_ms() - last_ms) / 1000), missed, MISSED_MAX);
    else
        printf("presence: no check-in yet\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_presence, presence, device ID and the last server check-in);
