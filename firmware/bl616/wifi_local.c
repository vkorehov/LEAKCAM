/*
 * The setup firmware's Wi-Fi (DESIGN.md 5.3.1, tests/test120): the wifi_link.h interface for the BL616's own station
 * only, no NetHub bridge (the operational firmware links wifi_link.c instead; NetHub and the low-power firmware's
 * 30 KB of the Wi-Fi RAM do not fit together). wifi_check.c (the BLE credential check) runs on it unchanged.
 *
 * lwIP starts at boot (wifi_local_start()), Wi-Fi at its first use (warm-up or check), as wifi_link.c: BLE starts
 * first. The BLE controller's init never returned when it ran after fhost_init() in this low-power build (test120,
 * 2026-11-01: stuck in btble_controller_init; the SDK example and the power test firmware start BLE before Wi-Fi).
 * Nobody else owns the radio, so a claim always succeeds.
 */
#include "wifi_link.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include <lwip/tcpip.h>

#include "async_event.h"
#include "easyflash.h"
#include "fhost_api.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"

#include "wifi_ctrl_proto.h"

#define IDLE_MS 10000                       /* as wifi_link.c: a supplicant scan cycle (~8 s) and its scan */

static void (*volatile local_ev)(int code);
static volatile bool mgmr_ready;
static bool wifi_up;

/* breadcrumbs (wifi_link.c): empty here */
__attribute__((weak)) void wl_trace(uint8_t code)
{
    (void)code;
}

/* the Wi-Fi manager's callback: never blocks (wifi_link.c wifi_event()) */
static void wifi_event(async_input_event_t ev, void *priv)
{
    void (*check)(int) = local_ev;
    int code = (int)ev->code;

    (void)priv;
    if (code == CODE_WIFI_ON_INIT_DONE) {
        if (wifi_mgmr_task_start() != 0)
            vAssertCalled();
        return;
    }
    if (code == CODE_WIFI_ON_MGMR_DONE)
        mgmr_ready = true;
    if (check)
        check(code);
}

void wifi_local_start(void)
{
    tcpip_init(NULL, NULL);
    if (async_register_event_filter(EV_WIFI, wifi_event, NULL) != 0)
        vAssertCalled();
}

/* as wifi_link.c start_wifi(); the rest follows from wifi_event() */
static void start_wifi(void)
{
    if (wifi_up)
        return;
    wifi_up = true;
    wifi_task_create();
    vTaskDelay(pdMS_TO_TICKS(500));
    if (fhost_init() != 0)
        vAssertCalled();
}

bool wifi_link_claim(enum wifi_owner who)
{
    (void)who;
    return true;
}

void wifi_link_release(enum wifi_owner who)
{
    (void)who;
}

void wifi_link_credentials_changed(void)
{
}

bool wifi_link_has_credentials(void)
{
    char ssid[WCP_SSID_MAX];
    return ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid), NULL) > 0;
}

/* as wifi_link.c: a connect into a busy station never returns, so leave until the supplicant is idle */
bool wifi_link_sta_leave(void)
{
    (void)wifi_mgmr_sta_autoconnect_disable();
    for (int ms = 0; ms < IDLE_MS; ms += 20) {
        if (ms % 1000 == 0)
            (void)wifi_sta_disconnect();
        vTaskDelay(pdMS_TO_TICKS(20));
        if (wifi_mgmr_sta_state_get_ext() <= 2)
            return true;
    }
    return false;
}

void wifi_link_local_begin(void (*ev)(int code))
{
    local_ev = ev;
    if (!wifi_up)
        start_wifi();               /* ev gets CODE_WIFI_ON_MGMR_DONE */
    else if (mgmr_ready)
        ev(CODE_WIFI_ON_MGMR_DONE);
}

void wifi_link_local_end(void)
{
    local_ev = NULL;
}

/* a phone connected: Wi-Fi up now, so the check starts at once (wifi_link.h); blocks ~1 s, ble_pairing.c's own task */
void wifi_link_warm(void)
{
    start_wifi();
}

/* Wi-Fi stays up once started (the setup firmware has nothing else for the radio) */
void wifi_link_cool(void)
{
}
