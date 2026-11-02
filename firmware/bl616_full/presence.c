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
#include "switch_to_setup.h"
#include "wifi_check.h"

#define POLL_S      60                       /* until the server says otherwise */
#define POLL_MIN_S  5
#define POLL_MAX_S  3600

static uint32_t poll_s = POLL_S;
static enum wifi_check_state last = WCHK_IDLE;
static uint16_t last_detail;
static uint32_t last_ms;

static void no_progress(enum wifi_check_state st, uint16_t detail)
{
    (void)st;
    (void)detail;
}

static void check_in(void)
{
    char ssid[33], psk[64], path[API_PATH_CAP], body[64];
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
    const char *p = last == WCHK_INTERNET ? strstr(body, "\"poll\":") : NULL;
    if (p) {
        long v = strtol(p + 7, NULL, 10);
        if (v >= POLL_MIN_S && v <= POLL_MAX_S)
            poll_s = (uint32_t)v;
    }
    if (last != WCHK_INTERNET)
        switch_to_setup("lost");          /* presence lost, for whatever reason (tests/test117) */
    else if (strstr(body, "\"ble\":1"))
        switch_to_setup("wake");          /* a phone out of range asked through the server (tests/test119) */
}

static void presence_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));         /* online within seconds of the start */
    for (;;) {
        check_in();                        /* on USB and on battery; it needs a stored network */
        vTaskDelay(pdMS_TO_TICKS(poll_s * 1000));
    }
}

void presence_start(void)
{
    if (xTaskCreate(presence_task, "pres", 2048, NULL, 2, NULL) != pdPASS)
        vAssertCalled();
}

static void cmd_presence(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("presence: id %s, server %s:%s, poll %lu s\r\n", device_id(), API_HOST, API_PORT,
           (unsigned long)poll_s);
    if (last_ms)
        printf("presence: last check-in state 0x%02x detail %u, %lu s ago\r\n", last, last_detail,
               (unsigned long)(((uint32_t)bflb_mtimer_get_time_ms() - last_ms) / 1000));
    else
        printf("presence: no check-in yet\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_presence, presence, device ID and the last server check-in);
