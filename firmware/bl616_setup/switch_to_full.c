#include "switch_to_full.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "easyflash.h"

#include "api.h"
#include "ble_pairing.h"
#include "metrics.h"
#include "ota.h"
#include "slot.h"
#include "usb_setup.h"
#include "wifi_check.h"

#define SWITCH_TO_FULL_PHONE_MS 15000   /* the phone reads SAVED, then leaves; a reset under it looked like a failure */
#define SWITCH_TO_FULL_WAKE_S   120     /* a phone's wake: time to find the board and tap Change (tests/test119) */
#define SWITCH_TO_FULL_USB_MS   2000    /* the cable setup: the K230 relays STATUS SAVED, then goes off */
#define SWITCH_TO_FULL_RETRY_S  (6 * 3600)   /* presence lost: the stored network tried again (DESIGN.md 5.3.1) */

static volatile bool running;

static void no_progress(enum wifi_check_state st, uint16_t detail)
{
    (void)st;
    (void)detail;
}

static void switch_now(void)
{
    if (ota_blocks_full()) {                /* slot 0 waits for its upgrade or is half written (ota.c) */
        printf("switch_to_full: the full firmware's upgrade first, staying in setup\r\n");
        return;
    }
    if (usb_setup_active()) {               /* the relay ends with the K230's power first: counted in the check-in */
        usb_setup_end("back to the full firmware");   /* (metrics.h); the reset alone cut it unrecorded (test048) */
        while (usb_setup_active())
            vTaskDelay(pdMS_TO_TICKS(50));
    }
    metrics_switch();
    if (slot_switch() != 0)                 /* returns only on a failed partition-table write */
        printf("switch_to_full: slot switch failed, staying in setup\r\n");
}

static void usb_task(void *arg)
{
    char ssid[33], psk[64], path[API_PATH_CAP];
    size_t ss = 0, ps = 0;
    struct wifi_check_target api;
    uint16_t detail;

    (void)arg;
    size_t sl = ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid) - 1, &ss);
    size_t pl = ef_get_env_blob("wifi_psk", psk, sizeof(psk) - 1, &ps);
    if (ss == 0) {
        printf("switch_to_full: USB power, no network stored: setup stays\r\n");
        usb_setup_begin();
    } else {
        api_poll_target(&api, path, ssid, sl, NULL, 0);
        enum wifi_check_state st = wifi_check_run(ssid, sl, psk, ps ? pl : 0, &wifi_check_probe, &api, no_progress,
                                                  &detail);
        if (st == WCHK_INTERNET)
            switch_now();                   /* never returns on success */
        else {
            printf("switch_to_full: USB power, the stored network failed its check (state 0x%02x detail %u): setup stays\r\n",
                   st, detail);
            usb_setup_begin();
        }
    }
    running = false;
    vTaskDelete(NULL);
}

static void credentials_task(void *arg)
{
    (void)arg;
    for (uint32_t ms = 0; ble_pairing_connected() && ms < SWITCH_TO_FULL_PHONE_MS; ms += 100)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (usb_setup_active()) {
        vTaskDelay(pdMS_TO_TICKS(SWITCH_TO_FULL_USB_MS));   /* the SAVED line reaches the phone */
        usb_setup_end("network saved");
        while (usb_setup_active())
            vTaskDelay(pdMS_TO_TICKS(50));
    }
    switch_now();
    running = false;
    vTaskDelete(NULL);
}

static void start(TaskFunction_t fn, const char *name)
{
    taskENTER_CRITICAL();
    bool busy = running;
    running = true;
    taskEXIT_CRITICAL();
    if (busy)
        return;
    if (xTaskCreate(fn, name, 2048, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
}

void switch_to_full_on_usb(void)
{
    start(usb_task, "hand_usb");
}

void switch_to_full_after_credentials(void)
{
    start(credentials_task, "hand_cred");
}

static void back_task(void *arg)
{
    uint32_t s = (uint32_t)(uintptr_t)arg;
    for (;;) {
        vTaskDelay((TickType_t)s * configTICK_RATE_HZ);   /* pdMS_TO_TICKS overflows 32 bits at 6 h */
        while (ble_pairing_connected() || running)
            vTaskDelay(pdMS_TO_TICKS(1000));
        printf("switch_to_full: %lu s in setup: back to the full firmware\r\n", (unsigned long)s);
        switch_now();                       /* returns only on a failed partition-table write: try again later */
        s = SWITCH_TO_FULL_RETRY_S;
    }
}

bool switch_to_full_start(void)
{
    char why[8] = { 0 }, ssid[2];
    size_t sent = 0, stored = 0;
    (void)ef_get_env_blob("setup_why", why, sizeof(why) - 1, &sent);
    if (sent && ef_del_env("setup_why") != EF_NO_ERR)
        vAssertCalled();                    /* a flash error: kept, it would decide the next start too */
    bool wake = sent && !strcmp(why, "wake");
    (void)ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid), &stored);
    if (stored) {
        uint32_t s = wake ? SWITCH_TO_FULL_WAKE_S : SWITCH_TO_FULL_RETRY_S;
        printf("switch_to_full: back to the full firmware in %lu s%s\r\n", (unsigned long)s, wake ? " (a phone's wake)" : "");
        if (xTaskCreate(back_task, "hand_back", 1024, (void *)(uintptr_t)s, 3, NULL) != pdPASS)
            vAssertCalled();
    }
    return sent > 0;
}
