#include "presence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "easyflash.h"
#include "shell.h"

#include "ble_pairing.h"
#include "device_id.h"
#include "usb_power.h"
#include "wifi_check.h"

/* the bench server: server/ on the Pi (vhome, systemd unit leakcam-server, server/deploy.sh); the product takes the
 * address from the K230 agent later (DESIGN 5.1) */
#define SERVER_HOST "192.168.1.159"
#define SERVER_PORT "8090"
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
    char ssid[33], psk[64], path[32], body[64];
    size_t ss = 0, ps = 0;
    size_t sl = ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid) - 1, &ss);
    size_t pl = ef_get_env_blob("wifi_psk", psk, sizeof(psk) - 1, &ps);
    if (ss == 0)
        return;                              /* not set up yet */
    snprintf(path, sizeof(path), "/v1/poll?id=%s", device_id());
    struct wifi_check_target t = { SERVER_HOST, SERVER_PORT, path, 200, body, sizeof(body) };
    last = wifi_check_run(ssid, sl, psk, ps ? pl : 0, &t, no_progress, &last_detail);
    last_ms = (uint32_t)bflb_mtimer_get_time_ms();
    const char *p = last == WCHK_INTERNET ? strstr(body, "\"poll\":") : NULL;
    if (p) {
        long v = strtol(p + 7, NULL, 10);
        if (v >= POLL_MIN_S && v <= POLL_MAX_S)
            poll_s = (uint32_t)v;
    }
}

static void presence_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));         /* BLE up first; then online within seconds of the plug */
    for (;;) {
        if (usb_present() && !ble_pairing_connected())
            check_in();
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
    printf("presence: id %s, server %s:%s, poll %lu s\r\n", device_id(), SERVER_HOST, SERVER_PORT,
           (unsigned long)poll_s);
    if (last_ms)
        printf("presence: last check-in state 0x%02x detail %u, %lu s ago\r\n", last, last_detail,
               (unsigned long)(((uint32_t)bflb_mtimer_get_time_ms() - last_ms) / 1000));
    else
        printf("presence: no check-in yet\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_presence, presence, device ID and the last server check-in);
