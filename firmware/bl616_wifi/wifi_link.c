/*
 * LEAKCAM BL616 Wi-Fi link: Bouffalo NetHub as an Ethernet bridge between the station
 * interface and the K230 on SDIO, plus the control protocol of wifi_ctrl_proto.h.
 *
 * Who does what:
 *   BL616  radio, WPA (the supplicant needs the EAPOL frames, so only those stay here),
 *          association with the credentials stored over BLE, reconnect after a loss
 *   K230   everything IP: DHCP, ARP, ICMP, DNS, NTP, sockets, all with the BL616's MAC
 * NetHub's built-in receive policy is for the opposite split (the BL616 keeps DHCP, ICMP and
 * its own IP address), so it is replaced by rx_filter() below, and the station joins with
 * use_dhcp = 0: a BL616 DHCP client with the same MAC would fight the K230's.
 *
 * Control messages arrive in NetHub's virtual-channel task and Wi-Fi events in the SDK's
 * event task; both are short and only call the Wi-Fi manager's asynchronous API, so no task of
 * our own is needed.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <lwip/prot/ethernet.h>

#include "async_event.h"
#include "bflb_gpio.h"
#include "bflb_mtd.h"
#include "bl616_glb.h"
#include "bl_fw_api.h"
#include "easyflash.h"
#include "fhost_api.h"
#include "rfparam_adapter.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"

#include "mr_sdio_drv.h"
#include "nethub.h"
#include "nethub_filter.h"
#include "nethub_vchan.h"

#include "board_pins.h"
#include "wifi_ctrl_proto.h"
#include "wifi_link.h"

#define DBG_TAG "WIFI"
#include "log.h"

#define ETHTYPE_EAPOL 0x888eU

/* the protocol's auth numbers are the SDK's; say so where both are visible */
_Static_assert(WCP_AUTH_WPA2_WPA3 == WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK_WPA3_SAE &&
               WCP_AUTH_UNKNOWN == WIFI_EVENT_BEACON_IND_AUTH_UNKNOWN, "wcp_auth out of step");

static volatile uint8_t state = WCP_STATE_DOWN;
static volatile bool want_link;     /* a join was asked for and not cancelled by WCP_LEAVE */
static volatile bool host_seen;     /* the K230 has spoken since we booted */
static volatile bool scan_pending;
static bool booted;

/* ------------------------------------------------------------------ K230 side */

static void send_status(void)
{
    struct wcp_status s;
    wifi_mgmr_connect_ind_stat_info_t info;
    int rssi = 0;

    memset(&s, 0, sizeof(s));
    s.type = WCP_STATUS;
    s.state = state;
    if (state == WCP_STATE_UP) {
        wifi_mgmr_sta_rssi_get(&rssi);
        s.rssi = (int8_t)rssi;
        memset(&info, 0, sizeof(info));
        if (wifi_mgmr_sta_connect_ind_stat_get(&info) == 0) {
            s.channel = info.channel;
            memcpy(s.bssid, info.bssid, sizeof(s.bssid));
            s.ssid_len = (uint8_t)strnlen(info.ssid, WCP_SSID_MAX);
            memcpy(s.ssid, info.ssid, s.ssid_len);
        }
    }
    if (nethub_vchan_user_send(&s, sizeof(s)) != 0)
        LOG_W("status not sent\r\n");
}

static void set_state(uint8_t s)
{
    if (s == state)
        return;
    state = s;
    /* before the K230 asks, nobody is listening: a message queued now would reach the next
     * host session as stale data, and the first WCP_GET_STATUS gets the current state anyway */
    if (host_seen)
        send_status();
}

/* ------------------------------------------------------------------ Wi-Fi */

static int join(const char *ssid, uint8_t ssid_len, const char *psk, uint8_t psk_len)
{
    wifi_mgmr_sta_connect_params_t p;

    memset(&p, 0, sizeof(p));
    memcpy(p.ssid, ssid, ssid_len);
    p.ssid_len = ssid_len;
    memcpy(p.key, psk, psk_len);
    p.key_len = psk_len;
    p.pmf_cfg = 1;      /* PMF capable, the SDK AT module's default for this Wi-Fi stack */
    p.use_dhcp = 0;     /* DHCP is the K230's, through the bridge */

    want_link = true;
    wifi_mgmr_sta_autoconnect_enable();
    set_state(WCP_STATE_JOINING);
    if (wifi_mgmr_sta_connect(&p) != 0) {
        LOG_E("connect request refused\r\n");
        set_state(WCP_STATE_FAILED);
        return -1;
    }
    return 0;
}

/* credentials written by BLE provisioning (bl616_pwrmgr/ble_pairing.c), no terminating NUL */
static void join_stored(void)
{
    char ssid[WCP_SSID_MAX], psk[WCP_PSK_MAX];
    size_t ssid_len, psk_len;

    ssid_len = ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid), NULL);
    psk_len = ef_get_env_blob("wifi_psk", psk, sizeof(psk), NULL);
    if (ssid_len == 0) {
        LOG_I("no stored Wi-Fi credentials, waiting for the K230\r\n");
        return;
    }
    LOG_I("joining \"%.*s\"\r\n", (int)ssid_len, ssid);
    join(ssid, (uint8_t)ssid_len, psk, (uint8_t)psk_len);
}

static void scan_item(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
    struct wcp_scan_item m;
    uint8_t *count = arg;

    (void)env;
    memset(&m, 0, sizeof(m));
    m.type = WCP_SCAN_ITEM;
    m.channel = item->channel;
    m.rssi = item->rssi;
    m.auth = item->auth;
    memcpy(m.bssid, item->bssid, sizeof(m.bssid));
    m.ssid_len = (uint8_t)(item->ssid_len > WCP_SSID_MAX ? WCP_SSID_MAX : item->ssid_len);
    memcpy(m.ssid, item->ssid, m.ssid_len);
    if (nethub_vchan_user_send(&m, sizeof(m)) == 0 && *count < 255)
        (*count)++;
}

static void scan_done(void)
{
    struct wcp_scan_done d = { WCP_SCAN_DONE, 0 };

    scan_pending = false;
    wifi_mgmr_scan_ap_all(NULL, &d.count, scan_item);
    nethub_vchan_user_send(&d, sizeof(d));
}

static void wifi_event(async_input_event_t ev, void *priv)
{
    (void)priv;
    switch (ev->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_task_start();
            break;
        case CODE_WIFI_ON_MGMR_DONE:
            join_stored();
            break;
        case CODE_WIFI_ON_CONNECTING:
            if (want_link)
                set_state(WCP_STATE_JOINING);
            break;
        case CODE_WIFI_ON_CONNECTED:
            set_state(WCP_STATE_UP);
            break;
        case CODE_WIFI_ON_DISCONNECT:
            /* a loss while up is the start of a reconnect; a loss while joining is a failed
             * attempt (the manager's autoconnect decides whether it tries again) */
            if (!want_link)
                set_state(WCP_STATE_DOWN);
            else
                set_state(state == WCP_STATE_UP ? WCP_STATE_JOINING : WCP_STATE_FAILED);
            break;
        case CODE_WIFI_ON_SCAN_DONE:
            if (scan_pending)
                scan_done();
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ control channel */

static int ctrl_rx(void *arg, uint8_t *data, uint16_t len)
{
    const struct wcp_join *j = (const struct wcp_join *)data;
    wifi_mgmr_scan_params_t sp;

    (void)arg;
    if (len < 1)
        return 0;
    host_seen = true;
    switch (data[0]) {
        case WCP_GET_STATUS:
            send_status();
            break;
        case WCP_JOIN:
            if (len < sizeof(*j) || j->ssid_len == 0 || j->ssid_len > WCP_SSID_MAX ||
                j->psk_len > WCP_PSK_MAX) {
                set_state(WCP_STATE_FAILED);
                break;
            }
            join(j->ssid, j->ssid_len, j->psk, j->psk_len);
            break;
        case WCP_LEAVE:
            want_link = false;
            wifi_mgmr_sta_autoconnect_disable();
            wifi_sta_disconnect();
            set_state(WCP_STATE_DOWN);
            break;
        case WCP_SCAN:
            memset(&sp, 0, sizeof(sp));    /* all channels, active */
            scan_pending = true;
            if (wifi_mgmr_sta_scan(&sp) != 0)
                scan_done();               /* reports what the manager still has, maybe 0 */
            break;
        default:
            break;
    }
    return 0;
}

/*
 * Everything but EAPOL goes to the K230. EAPOL must reach the BL616's supplicant. ARP, DHCP
 * and ICMP are the K230's: the BL616 has no IP address of its own.
 */
static nethub_wifi_rx_filter_action_t rx_filter(nethub_channel_t src, const struct pbuf *p,
                                                void *ctx)
{
    const struct eth_hdr *eth = (const struct eth_hdr *)p->payload;

    (void)src;
    (void)ctx;
    if (p->len >= SIZEOF_ETH_HDR && eth->type == PP_HTONS(ETHTYPE_EAPOL))
        return NETHUB_WIFI_RX_FILTER_LOCAL;
    return NETHUB_WIFI_RX_FILTER_HOST;
}

/*
 * SDIO pads at drive strength 0 instead of the SDK's 1 (board_sdio_gpio_init()). Datasheet
 * 7.2.2, GPIO 0-20: DRV_0 sources 9.7 mA / sinks 11.4 mA at the 10 % VOH/VOL limit, about 35 ohm,
 * close to the ~49 ohm SDIO traces (35-52 mm), so the driver itself source-terminates them;
 * DRV_1 is about 11 ohm and rings. Called after every SDK call that muxes the pads.
 */
static void sdio_pins_low_drive(void)
{
    static const uint8_t pins[] = { PIN_SD_D2, PIN_SD_D3, PIN_SD_CMD, PIN_SD_CLK, PIN_SD_D0, PIN_SD_D1 };
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");

    for (unsigned i = 0; i < sizeof(pins); i++)
        bflb_gpio_init(gpio, pins[i], GPIO_FUNC_SDU | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN |
                                          GPIO_DRV_0);
}

/* ------------------------------------------------------------------ entry points */

int wifi_link_start(void)
{
    int ret;

    if (booted) {
        ret = mr_sdio_drv_lowpower_restore();    /* K230 powered up again: re-mux and resync */
        sdio_pins_low_drive();
        return ret;
    }

    if (rfparam_init(0, NULL, 0) != 0) {
        LOG_E("RF init failed\r\n");
        return -1;
    }
    tcpip_init(NULL, NULL);
    bflb_mtd_init();
    easyflash_init();

    /* NetHub first: the SDU must be answering by the time the K230's card detection runs */
    nethub_set_wifi_rx_filter(rx_filter, NULL);
    ret = nethub_bootstrap();
    if (ret != NETHUB_OK) {
        LOG_E("nethub bootstrap failed: %d\r\n", ret);
        return ret;
    }
    sdio_pins_low_drive();
    nethub_vchan_user_recv_register(ctrl_rx, NULL);

    /* Wi-Fi start as in the SDK's NetHub example: the rest follows from wifi_event() */
    GLB_Set_EM_Sel(GLB_WRAM160KB_EM0KB);
    async_register_event_filter(EV_WIFI, wifi_event, NULL);
    wifi_task_create();
    vTaskDelay(pdMS_TO_TICKS(500));
    fhost_init();
    booted = true;
    return 0;
}

void wifi_link_stop(void)
{
    /* SDU off and its pads released; k230_power_off() then parks them in analog mode */
    if (booted)
        mr_sdio_drv_lowpower_prepare();
}
