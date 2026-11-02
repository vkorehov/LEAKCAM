/*
 * The setup firmware's Wi-Fi (DESIGN.md 5.3.1, tests/test120): the wifi_link.h interface for the BL616's own station
 * only, no NetHub bridge (the full firmware links firmware/bl616_full/wifi_link.c instead; NetHub and the low-power
 * firmware's 30 KB of the Wi-Fi RAM do not fit together). wifi_check.c (the BLE credential check) runs on it unchanged.
 *
 * Wi-Fi starts at boot in its own task, as the SDK's low-power example does, with the BLE start running during its
 * 500 ms pause (setup_boot.c): the order proven on rev 1 (test120, 2026-11-01). Nobody else owns the radio, so a
 * claim always succeeds.
 */
#include "wifi_setup.h"
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

/* as wifi_link.c start_wifi(); the rest follows from wifi_event() */
static void wifi_start_task(void *arg)
{
    (void)arg;
    wifi_task_create();
    vTaskDelay(pdMS_TO_TICKS(500));
    if (fhost_init() != 0)
        vAssertCalled();
    vTaskDelete(NULL);
}

void wifi_setup_start(void)
{
    tcpip_init(NULL, NULL);
    if (async_register_event_filter(EV_WIFI, wifi_event, NULL) != 0 ||
        xTaskCreate(wifi_start_task, "wifi init", 1024, NULL, 27, NULL) != pdPASS)
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
    if (mgmr_ready)
        ev(CODE_WIFI_ON_MGMR_DONE);  /* else ev gets it from wifi_event() */
}

void wifi_link_local_end(void)
{
    local_ev = NULL;
}

/* Wi-Fi is started at boot: nothing to warm */
void wifi_link_warm(void)
{
}

/* Wi-Fi stays up once started (the setup firmware has nothing else for the radio) */
void wifi_link_cool(void)
{
}
