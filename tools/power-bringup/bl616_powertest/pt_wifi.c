/*
 * test100 step 1: the BL616's radio alone, no SDIO, no K230 - the antenna and the RF before the bridge is built on it.
 *
 *   pt_wifi scan                  every AP heard: SSID, channel, RSSI, auth, BSSID; time to the result
 *   pt_wifi join <ssid> [psk]     associate + WPA, then DHCP on the BL616's own lwIP; each step timed
 *   pt_wifi status                joined?, RSSI, channel, IP / gateway / DNS
 *   pt_wifi leave                 DHCP lease released, disconnected
 *
 * The bring-up order is the product's (firmware/bl616/main.c radio_init(), wifi_link.c start_lwip/start_wifi): RF
 * parameters, lwIP, the Wi-Fi task + fhost; the manager answers once CODE_WIFI_ON_MGMR_DONE came. The join and the
 * DHCP follow wifi_check.c (the product's credential check), which this step proves on hardware first. Later steps
 * (TCP, the NetHub bridge to the K230) are added here one by one, each after the previous one passed.
 */
#include "tests.h"

#if PT_TEST_WIFI
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FreeRTOS.h>
#include <event_groups.h>
#include <task.h>

#include <lwip/tcpip.h>
#include <lwip/ip4_addr.h>

#include "async_event.h"
#include "bflb_mtimer.h"
#include "easyflash.h"
#include "fhost_api.h"
#include "macsw_plat.h"
#include "net_al_ext.h"
#include "rfparam_adapter.h"
#include "shell.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"

#define EV_READY        (1u << 0)
#define EV_SCANNED      (1u << 1)
#define EV_CONNECTED    (1u << 2)
#define EV_DISCONNECTED (1u << 3)
#define EV_GOT_IP       (1u << 4)
#define EV_IP_TIMEOUT   (1u << 5)

#define READY_MS 10000
#define SCAN_MS  10000
#define JOIN_MS  25000
#define DHCP_MS  20000
#define LEAVE_MS 3000

static EventGroupHandle_t evg;
static bool up;
static uint32_t up_ms;              /* bring-up time of this boot's first use */

static void wifi_event(async_input_event_t ev, void *priv)
{
    EventBits_t b = 0;

    (void)priv;
    switch (ev->code) {
        case CODE_WIFI_ON_INIT_DONE: wifi_mgmr_task_start(); break;
        case CODE_WIFI_ON_MGMR_DONE: b = EV_READY; break;
        case CODE_WIFI_ON_SCAN_DONE: b = EV_SCANNED; break;
        case CODE_WIFI_ON_CONNECTED: b = EV_CONNECTED; break;
        case CODE_WIFI_ON_DISCONNECT: b = EV_DISCONNECTED; break;
        case CODE_WIFI_ON_GOT_IP: b = EV_GOT_IP; break;
        case CODE_WIFI_ON_GOT_IP_TIMEOUT: b = EV_IP_TIMEOUT; break;
        default: break;
    }
    if (b)
        xEventGroupSetBits(evg, b);
}

static EventBits_t wait(EventBits_t bits, uint32_t ms)
{
    return xEventGroupWaitBits(evg, bits, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)) & bits;
}

/* first use in this boot: RF parameters, lwIP, the Wi-Fi task; false if the manager did not come up */
static bool bring_up(void)
{
    if (up)
        return true;
    uint64_t t0 = bflb_mtimer_get_time_ms();
    evg = xEventGroupCreate();
    easyflash_init();                       /* rfparam reads its calibration through the partition table */
    if (rfparam_init(0, NULL, 0) != 0) {
        printf("pt_wifi: RF init failed\r\n");
        return false;
    }
    tcpip_init(NULL, NULL);
    async_register_event_filter(EV_WIFI, wifi_event, NULL);
    wifi_task_create();
    vTaskDelay(pdMS_TO_TICKS(500));         /* as the SDK examples: the task runs before fhost_init() */
    fhost_init();
    if (!wait(EV_READY, READY_MS)) {
        printf("pt_wifi: the Wi-Fi manager did not come up in %u ms\r\n", READY_MS);
        return false;
    }
    up = true;
    up_ms = (uint32_t)(bflb_mtimer_get_time_ms() - t0);
    printf("pt_wifi: radio up in %lu ms (RF params, lwIP, Wi-Fi task, manager)\r\n", (unsigned long)up_ms);
    return true;
}

static const char *auth_name(uint8_t a)
{
    switch (a) {
        case WIFI_EVENT_BEACON_IND_AUTH_OPEN: return "open";
        case WIFI_EVENT_BEACON_IND_AUTH_WEP: return "WEP";
        case WIFI_EVENT_BEACON_IND_AUTH_WPA_PSK: return "WPA";
        case WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_EVENT_BEACON_IND_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_EVENT_BEACON_IND_AUTH_WPA3_SAE: return "WPA3";
        case WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK_WPA3_SAE: return "WPA2/WPA3";
        default: return "?";
    }
}

static void scan_item(void *env, void *arg, wifi_mgmr_scan_item_t *it)
{
    unsigned *n = arg;

    (void)env;
    (*n)++;
    printf("  %-32.*s ch %2u  %4d dBm  %-9s %02x:%02x:%02x:%02x:%02x:%02x\r\n", it->ssid_len < 0 ? 0 : it->ssid_len,
           it->ssid, it->channel, it->rssi, auth_name(it->auth), it->bssid[0], it->bssid[1], it->bssid[2],
           it->bssid[3], it->bssid[4], it->bssid[5]);
}

static void do_scan(void)
{
    wifi_mgmr_scan_params_t sp;
    unsigned n = 0;

    memset(&sp, 0, sizeof(sp));
    uint64_t t0 = bflb_mtimer_get_time_ms();
    xEventGroupClearBits(evg, EV_SCANNED);
    if (wifi_mgmr_sta_scan(&sp) != 0 || !wait(EV_SCANNED, SCAN_MS))
        printf("pt_wifi scan: did not finish in %u ms, listing what the manager has\r\n", SCAN_MS);
    uint32_t ms = (uint32_t)(bflb_mtimer_get_time_ms() - t0);
    wifi_mgmr_scan_ap_all(NULL, &n, scan_item);
    printf("pt_wifi scan: %u APs in %lu ms\r\n", n, (unsigned long)ms);
}

static void print_ip(void)
{
    uint32_t a = 0, m = 0, g = 0, d = 0;
    wifi_sta_ip4_addr_get(&a, &m, &g, &d);
    printf("  ip %s", ip4addr_ntoa((const ip4_addr_t *)&a));
    printf("  mask %s", ip4addr_ntoa((const ip4_addr_t *)&m));
    printf("  gw %s", ip4addr_ntoa((const ip4_addr_t *)&g));
    printf("  dns %s\r\n", ip4addr_ntoa((const ip4_addr_t *)&d));
}

static void do_join(const char *ssid, const char *psk)
{
    wifi_mgmr_sta_connect_params_t p;
    size_t sl = strlen(ssid), pl = psk ? strlen(psk) : 0;

    if (sl > sizeof(p.ssid) || pl > sizeof(p.key)) {
        printf("pt_wifi join: SSID or key too long\r\n");
        return;
    }
    memset(&p, 0, sizeof(p));
    memcpy(p.ssid, ssid, sl);
    p.ssid_len = (uint8_t)sl;
    memcpy(p.key, psk, pl);
    p.key_len = (uint8_t)pl;
    p.pmf_cfg = 1;
    p.use_dhcp = 1;
    p.dhcp_timeout_event_only = 1;
    xEventGroupClearBits(evg, EV_CONNECTED | EV_DISCONNECTED | EV_GOT_IP | EV_IP_TIMEOUT);
    uint64_t t0 = bflb_mtimer_get_time_ms();
    if (wifi_mgmr_sta_connect(&p) != 0) {
        printf("pt_wifi join: the manager refused the request\r\n");
        return;
    }
    EventBits_t b = wait(EV_CONNECTED | EV_DISCONNECTED, JOIN_MS);
    uint64_t t1 = bflb_mtimer_get_time_ms();
    if (!(b & EV_CONNECTED)) {
        wifi_mgmr_connect_ind_stat_info_t info;
        memset(&info, 0, sizeof(info));
        wifi_mgmr_sta_connect_ind_stat_get(&info);
        printf("pt_wifi join: not associated after %lu ms (status %u reason %u)\r\n", (unsigned long)(t1 - t0),
               info.status_code, info.reason_code);
        return;
    }
    printf("pt_wifi join: associated + WPA in %lu ms\r\n", (unsigned long)(t1 - t0));
    b = wait(EV_GOT_IP | EV_IP_TIMEOUT | EV_DISCONNECTED, DHCP_MS);
    uint64_t t2 = bflb_mtimer_get_time_ms();
    if (!(b & EV_GOT_IP)) {
        printf("pt_wifi join: no DHCP address after %lu ms\r\n", (unsigned long)(t2 - t1));
        return;
    }
    int rssi = 0;
    uint8_t ch = 0;
    wifi_mgmr_sta_rssi_get(&rssi);
    wifi_mgmr_sta_channel_get(&ch);
    printf("pt_wifi join: DHCP in %lu ms; join to address %lu ms; RSSI %d dBm, channel %u\r\n",
           (unsigned long)(t2 - t1), (unsigned long)(t2 - t0), rssi, ch);
    print_ip();
}

static void do_status(void)
{
    int rssi = 0;
    uint8_t ch = 0;

    if (!wifi_mgmr_sta_state_get()) {
        printf("pt_wifi status: not joined (radio up %s)\r\n", up ? "yes" : "no");
        return;
    }
    wifi_mgmr_sta_rssi_get(&rssi);
    wifi_mgmr_sta_channel_get(&ch);
    printf("pt_wifi status: joined, RSSI %d dBm, channel %u\r\n", rssi, ch);
    print_ip();
}

static void do_leave(void)
{
    xEventGroupClearBits(evg, EV_DISCONNECTED);
    net_al_ext_dhcp_disconnect();
    wifi_mgmr_sta_autoconnect_disable();
    if (wifi_sta_disconnect() == 0 && wait(EV_DISCONNECTED, LEAVE_MS))
        printf("pt_wifi leave: disconnected\r\n");
    else
        printf("pt_wifi leave: no disconnect event in %u ms\r\n", LEAVE_MS);
}

static int cmd_wifi(int argc, char **argv)
{
    const char *op = argc > 1 ? argv[1] : "";

    if (!strcmp(op, "scan") || !strcmp(op, "join") || !strcmp(op, "status") || !strcmp(op, "leave")) {
        if (!bring_up())
            return -1;
    }
    if (!strcmp(op, "scan"))
        do_scan();
    else if (!strcmp(op, "join") && argc > 2)
        do_join(argv[2], argc > 3 ? argv[3] : NULL);
    else if (!strcmp(op, "status"))
        do_status();
    else if (!strcmp(op, "leave"))
        do_leave();
    else
        printf("usage: pt_wifi scan | join <ssid> [psk] | status | leave\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_wifi, pt_wifi, test100 Wi-Fi on the BL616 alone: scan | join ssid psk | status | leave);
#endif
