#include "presence.h"

#include <ctype.h>
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

/* a JSON string value "<key>":"..." at or after `from`, unescaped into out (\" \\ \/ and \u00XX; anything else
 * refused: an SSID is 1-32 bytes, a passphrase 8-63 printable); its length, -1 when absent or refused */
static int answer_string(const char *from, const char *key, char *out, size_t cap)
{
    char k[24];
    snprintf(k, sizeof(k), "\"%s\":\"", key);
    const char *p = strstr(from, k);
    if (!p)
        return -1;
    size_t n = 0;
    for (p += strlen(k); *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            c = *++p;
            if (c == 'u' && p[1] == '0' && p[2] == '0' && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                char hex[3] = { p[3], p[4], 0 };
                c = (char)strtol(hex, NULL, 16);
                p += 4;
            } else if (c != '"' && c != '\\' && c != '/')
                return -1;
        }
        if (n + 1 >= cap)
            return -1;
        out[n++] = c;
    }
    if (*p != '"')
        return -1;
    out[n] = 0;
    return (int)n;
}

/* a new network from the server (DESIGN.md 5.3.13): "wifi":{"ssid":..,"psk":..} in the answer, tested with the whole
 * check (join, address, internet, the API: a check-in on it), stored only when all pass, else the old one kept; the
 * result reported at once (`wchg`, the server notifies the phone of a failure). true when one was tried */
static bool wifi_change(const char *answer)
{
    const char *w = strstr(answer, "\"wifi\":{");
    if (!w)
        return false;
    char ssid[33], psk[64], path[API_PATH_CAP], body[256];
    int sl = answer_string(w, "ssid", ssid, sizeof(ssid));
    int pl = answer_string(w, "psk", psk, sizeof(psk));
    if (sl < 1 || pl < 0 || (pl > 0 && pl < 8)) {
        printf("presence: a Wi-Fi change the board cannot read\r\n");
        metrics_wifi_change(WCHK_JOIN_FAILED);
        return true;
    }
    printf("presence: trying the new Wi-Fi \"%s\" from the server\r\n", ssid);   /* the SSID is not secret */
    struct wifi_check_target t;
    api_poll_target(&t, path, ssid, (size_t)sl, body, sizeof(body));
    uint16_t detail;
    enum wifi_check_state st = wifi_check_run(ssid, (size_t)sl, psk, (size_t)pl, &t, NULL, no_progress, &detail);
    if (st == WCHK_INTERNET && (ef_set_env_blob("wifi_ssid", ssid, (size_t)sl) != EF_NO_ERR ||
                                ef_set_env_blob("wifi_psk", psk, (size_t)pl) != EF_NO_ERR))
        st = WCHK_SAVE_FAILED;
    if (st == WCHK_INTERNET) {
        (void)ef_del_env("wifi_ap");         /* the old network's access point: the next join scans for the new one */
        st = WCHK_SAVED;                     /* reported as the app's setup reports it: checked and stored */
    }
    memset(psk, 0, sizeof(psk));
    printf("presence: the new Wi-Fi: state 0x%02x detail %u, %s\r\n", st, detail,
           st == WCHK_SAVED ? "stored" : "the old one kept");
    metrics_wifi_change(st);
    presence_now();                          /* reported now, on the network it is on */
    return true;
}

static void check_in(void)
{
    char ssid[33], psk[64], path[API_PATH_CAP], body[768];   /* an upgrade offer ~280 B, a network, media IDs */
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
    if (wifi_change(body))
        return;                           /* the rest of this answer waits for the next check-in, on its network */
    if (ota_fetch(body, ssid, sl, psk, ps ? pl : 0)) {
        switch_to_setup("ota");           /* the copy is in: the setup firmware unpacks it (DESIGN.md 5.3.7) */
        return;
    }
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
    if (xTaskCreate(presence_task, "pres", 3072, NULL, 2, &task) != pdPASS)   /* the answer and a new network's check */
        vAssertCalled();
}

void presence_now(void)
{
    if (task)
        xTaskNotifyGive(task);
}

/* `presence`: the state; `presence now`: a check-in now (the bench: a new interval taken at once, not after the old) */
static void cmd_presence(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "now")) {
        presence_now();
        printf("presence: check-in now\r\n");
        return;
    }
    printf("presence: id %s, server %s:%s, check-in every %lu s\r\n", device_id(), API_HOST, API_PORT,
           (unsigned long)checkin_s);
    if (last_ms)
        printf("presence: last check-in state 0x%02x detail %u, %lu s ago, missed %u of %u\r\n", last, last_detail,
               (unsigned long)(((uint32_t)bflb_mtimer_get_time_ms() - last_ms) / 1000), missed, MISSED_MAX);
    else
        printf("presence: no check-in yet\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_presence, presence, device ID and the last server check-in [now]);
