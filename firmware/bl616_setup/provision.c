#include "provision.h"

#include <string.h>

#include <FreeRTOS.h>
#include "task.h"

#include "easyflash.h"
#include "log.h"

#include "crash_rec.h"

#include "api.h"
#include "battery.h"
#include "ble_pairing.h"
#include "wifi_check.h"

static char ssid[33];
static char psk[64];
static uint8_t status[PROVISION_STATUS_LEN];
static uint32_t attempt;                 /* the phone's id of the last commit: STATUS answers for it */
static volatile bool checking;
static void (*listeners[2])(const uint8_t st[PROVISION_STATUS_LEN]);

static void set_status(enum wifi_check_state st, uint16_t detail)
{
    uint8_t next[PROVISION_STATUS_LEN] = { (uint8_t)st, (uint8_t)detail, (uint8_t)(detail >> 8) };
    for (int i = 0; i < 4; i++)
        next[3 + i] = (uint8_t)(attempt >> (8 * i));
    if (!memcmp(next, status, sizeof(status)))
        return;                          /* the same state again (JOINING from the commit, then from the check) */
    memcpy(status, next, sizeof(status));
    for (unsigned i = 0; i < 2; i++)
        if (listeners[i])
            listeners[i](status);
}

/* stored on the BL616: it is the Wi-Fi device, and the K230 is usually powered off */
static void store(const char *s, const char *k)
{
    if (ef_set_env_blob("wifi_ssid", s, strlen(s)) != EF_NO_ERR ||
        ef_set_env_blob("wifi_psk", k, strlen(k)) != EF_NO_ERR) {
        set_status(WCHK_SAVE_FAILED, 0);
        return;
    }
    LOG_I("provision: Wi-Fi credentials stored\r\n");
    set_status(WCHK_SAVED, 0);
    ble_credentials_stored();
}

static void progress(enum wifi_check_state st, uint16_t detail)
{
    set_status(st, detail);
}

/* the transport's wait before the check claims the radio (BLE: its warm-up); none by default */
__attribute__((weak)) void provision_before_check(void)
{
}

static void check_task(void *arg)
{
    static char s[33], k[64];            /* this check's copy: the phone may write again meanwhile */
    static char path[API_PATH_CAP];
    struct wifi_check_target api;
    uint16_t detail;

    (void)arg;
    provision_before_check();
    memcpy(s, ssid, sizeof(s));
    memcpy(k, psk, sizeof(k));
    /* stored only once the LEAKCAM API answers over this network (the check's end goal, DESIGN.md 5.3.1) */
    api_poll_target(&api, path, s, strlen(s), battery_mv(), battery_usb_power(), NULL, 0);
    enum wifi_check_state st = wifi_check_run(s, strlen(s), k, strlen(k), &wifi_check_probe, &api, progress, &detail);
    if (st != WCHK_INTERNET)
        set_status(st, detail);
    else                                 /* also with the phone gone: it reconnects and reads STATUS (tests/test110) */
        store(s, k);
    checking = false;
    vTaskDelete(NULL);
}

void provision_clear(void)
{
    memset(ssid, 0, sizeof(ssid));
    memset(psk, 0, sizeof(psk));
}

static int put(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap)
        return -1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

int provision_ssid(const char *s, size_t n)
{
    return put(ssid, sizeof(ssid), s, n);
}

int provision_psk(const char *k, size_t n)
{
    if (n > 0 && n < 8)                  /* WPA2 passphrase is 8..63 characters */
        return -1;
    return put(psk, sizeof(psk), k, n);
}

int provision_commit(uint8_t cmd, uint32_t id)
{
    if ((cmd != 1 && cmd != 2) || ssid[0] == 0)
        return -1;
    if (checking)
        return 0;                        /* the running check reports */
    attempt = id;
    status[0] = WCHK_IDLE;               /* a new commit: its first state is always reported, also with the same id */
    if (cmd == 2) {
        store(ssid, psk);
        return 0;
    }
    /* the check blocks for up to a minute: its own task, never the caller's (wifi_check_run claims the radio) */
    checking = true;
    set_status(WCHK_JOINING, 0);         /* a STATUS read after this commit's answer is this check's */
    if (xTaskCreate(check_task, "wchk", 1536, NULL, 3, NULL) != pdPASS)
        crash_rec_fatal("provision: check task not created");
    return 0;
}

void provision_status(uint8_t out[PROVISION_STATUS_LEN])
{
    memcpy(out, status, sizeof(status));
}

bool provision_checking(void)
{
    return checking;
}

void provision_on_status(void (*cb)(const uint8_t st[PROVISION_STATUS_LEN]))
{
    for (unsigned i = 0; i < 2; i++)
        if (!listeners[i]) {
            listeners[i] = cb;
            return;
        }
    crash_rec_fatal("provision: more than 2 status listeners");   /* a build error */
}
