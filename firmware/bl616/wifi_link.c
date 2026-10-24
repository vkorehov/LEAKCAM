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
#include <event_groups.h>
#include <queue.h>
#include <task.h>

#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <lwip/prot/ethernet.h>

#include "async_event.h"
#include "bflb_gpio.h"
#include "bl_fw_api.h"
#include "easyflash.h"
#include "fhost_api.h"
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
#define WL_EVB_DISC (1u << 0)               /* the manager reported a disconnect (set in wifi_event, waited in join) */
#define LEAVE_MS    3000
static EventGroupHandle_t wl_evg;
static volatile int own_disc;               /* DISCONNECT events still to come from our own leaves: not link losses */
static bool lwip_up, wifi_up, bridge_up;   /* tcpip_init(); Wi-Fi task + fhost; NetHub + SDIO */
static volatile uint8_t owner = WIFI_OWNER_NONE;
static volatile bool local_only;            /* the credential check: every frame to lwIP here */
static void (*volatile local_ev)(int code);
static volatile bool mgmr_ready;
static bool rejoin;                         /* a check dropped the bridge's association */

/* Breadcrumbs for a hang (tests/test100 step 2, 2026-10-23: the BL616 stopped dead after the control channel opened
 * and the watchdog reset it): empty here, the test firmware keeps the last marks in HBN RAM across the reset.
 * Codes: 0x1n ctrl_rx of WCP type n, 0x1f its end; 0x20/0x21 send_status in/out; 0x30/0x31 scan_done in/out, 0x32
 * per item, 0x33 an item's send failed, 0x34 the DONE's send failed, 0x35 a leave got no DISCONNECT in 3 s; 0x4n Wi-Fi event n (low bits); 0x5n set_state n;
 * 0x22/0x23 around each upload try; 0x60 join in, 0x61 autoconnect on, 0x62 connect called, 0x63 connect returned;
 * 0x3e a K230 message lost, 0x3f an event lost (the worker's queue full). */
__attribute__((weak)) void wl_trace(uint8_t code)
{
    (void)code;
}

/* ------------------------------------------------------------------ K230 side */

/*
 * The upload pool holds 5 messages, freed as the K230 reads them; nethub_vchan_user_send() waits 100 ms for one and
 * then fails (tests/test100, 2026-10-24: a scan with 7 APs waited 69 ms for items 6-7; with scans back to back items
 * and the DONE were dropped: the K230's list came up short and its join waited 10 s for a DONE that never came).
 * Wait up to VCHAN_SEND_MS. Only from a task (ctrl_rx, wl_ev_task), never from the Wi-Fi event callback.
 */
#define VCHAN_SEND_MS 2000

static int vchan_send(const void *msg, uint16_t len)
{
    for (int waited = 0; waited < VCHAN_SEND_MS; waited += 100) {
        wl_trace(0x22);
        int r = nethub_vchan_user_send(msg, len);   /* each try waits up to 100 ms for a buffer */
        wl_trace(0x23);
        if (r == 0)
            return 0;
    }
    return -1;
}

static void send_status(void)
{
    struct wcp_status s;
    wifi_mgmr_connect_ind_stat_info_t info;
    int rssi = 0;

    wl_trace(0x20);
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
    if (vchan_send(&s, sizeof(s)) != 0)
        LOG_W("status not sent\r\n");
    wl_trace(0x21);
}

static void set_state(uint8_t s)
{
    if (s == state)
        return;
    wl_trace(0x50 | (s & 0x0f));
    state = s;
    /* before the K230 asks, nobody is listening: a message queued now would reach the next
     * host session as stale data, and the first WCP_GET_STATUS gets the current state anyway */
    if (host_seen)
        send_status();
}

/* ------------------------------------------------------------------ Wi-Fi */

/* disconnect and wait for it, if associated or associating (wifi_check.c leave()): wifi_mgmr_sta_connect() on a busy
 * station never returned (2026-10-24). Not from the Wi-Fi event callback (it waits for an event). */
static void leave_and_wait(void)
{
    if (state != WCP_STATE_UP && state != WCP_STATE_JOINING)
        return;
    xEventGroupClearBits(wl_evg, WL_EVB_DISC);
    own_disc++;
    wifi_mgmr_sta_autoconnect_disable();
    if (wifi_sta_disconnect() != 0 ||
        !(xEventGroupWaitBits(wl_evg, WL_EVB_DISC, pdTRUE, pdFALSE, pdMS_TO_TICKS(LEAVE_MS)) & WL_EVB_DISC)) {
        own_disc = 0;                       /* no event came: none to ignore later */
        wl_trace(0x35);
    }
}

static int join(const char *ssid, uint8_t ssid_len, const char *psk, uint8_t psk_len)
{
    wifi_mgmr_sta_connect_params_t p;

    wl_trace(0x60);
    leave_and_wait();

    memset(&p, 0, sizeof(p));
    memcpy(p.ssid, ssid, ssid_len);
    p.ssid_len = ssid_len;
    memcpy(p.key, psk, psk_len);
    p.key_len = psk_len;
    p.pmf_cfg = 1;      /* PMF capable, the SDK AT module's default for this Wi-Fi stack */
    p.use_dhcp = 0;     /* DHCP is the K230's, through the bridge */

    want_link = true;
    wl_trace(0x61);
    wifi_mgmr_sta_autoconnect_enable();
    set_state(WCP_STATE_JOINING);
    wl_trace(0x62);
    int r = wifi_mgmr_sta_connect(&p);
    wl_trace(0x63);
    if (r != 0) {
        LOG_E("connect request refused\r\n");
        set_state(WCP_STATE_FAILED);
        return -1;
    }
    return 0;
}

/* credentials written by BLE provisioning (ble_pairing.c), no terminating NUL */
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

bool wifi_link_has_credentials(void)
{
    char ssid[WCP_SSID_MAX];
    return ef_get_env_blob("wifi_ssid", ssid, sizeof(ssid), NULL) > 0;
}

static void scan_item(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
    struct wcp_scan_item m;
    uint8_t *count = arg;

    (void)env;
    wl_trace(0x32);
    memset(&m, 0, sizeof(m));
    m.type = WCP_SCAN_ITEM;
    m.channel = item->channel;
    m.rssi = item->rssi;
    m.auth = item->auth;
    memcpy(m.bssid, item->bssid, sizeof(m.bssid));
    m.ssid_len = (uint8_t)(item->ssid_len > WCP_SSID_MAX ? WCP_SSID_MAX : item->ssid_len);
    memcpy(m.ssid, item->ssid, m.ssid_len);
    if (vchan_send(&m, sizeof(m)) != 0)
        wl_trace(0x33);
    else if (*count < 255)
        (*count)++;
}

static void scan_done(void)
{
    struct wcp_scan_done d = { WCP_SCAN_DONE, 0 };

    wl_trace(0x30);
    scan_pending = false;
    wifi_mgmr_scan_ap_all(NULL, &d.count, scan_item);
    if (vchan_send(&d, sizeof(d)) != 0)
        wl_trace(0x34);
    wl_trace(0x31);
}

/*
 * The Wi-Fi manager calls wifi_event() in a context where blocking calls never return (tests/test100 step 2,
 * 2026-10-23: the first nethub_vchan_user_send() of a scan result hung the BL616 until the watchdog; the same send
 * from the NetHub task works). So wifi_event() only does what cannot block (flags, the credential check's event
 * bits, the manager start) and queues the code; the bridge's handling, which sends to the K230, runs in wl_ev_task.
 */
#define WL_EV_DEPTH 8
#define WL_CTRL     (-1)                /* a K230 control message, not a Wi-Fi event */

/* The K230's control messages are handled here too, not in ctrl_rx (the NetHub virtual-channel task): a blocking
 * manager call there (wifi_mgmr_sta_connect() right after a leave never returned, 2026-10-24) stopped the channel:
 * no message consumed, no credit back to the K230, its scans failed "scan sync fail" until a reset. */
struct wl_item {
    int code;                           /* CODE_WIFI_ON_* or WL_CTRL */
    uint16_t len;
    uint8_t msg[sizeof(struct wcp_join)];   /* the largest WCP message */
};

static QueueHandle_t wl_ev_q;

static void bridge_event(int code);
static void ctrl_handle(const uint8_t *data, uint16_t len);

static void wl_ev_task(void *arg)
{
    struct wl_item it;

    (void)arg;
    while (1) {
        if (xQueueReceive(wl_ev_q, &it, portMAX_DELAY) != pdTRUE)
            continue;
        if (it.code == WL_CTRL)
            ctrl_handle(it.msg, it.len);
        else
            bridge_event(it.code);
    }
}

static void wifi_event(async_input_event_t ev, void *priv)
{
    void (*check)(int) = local_ev;
    int code = (int)ev->code;

    (void)priv;
    wl_trace(0x40 | (code & 0x0f));
    if (code == CODE_WIFI_ON_MGMR_DONE)
        mgmr_ready = true;
    if (code == CODE_WIFI_ON_DISCONNECT && wl_evg)
        xEventGroupSetBits(wl_evg, WL_EVB_DISC);
    if (code == CODE_WIFI_ON_INIT_DONE) {
        wifi_mgmr_task_start();
        return;
    }
    if (check) {
        /* the credential check owns the radio: its events, not the bridge's */
        check(code);
        return;
    }
    struct wl_item it = { .code = code, .len = 0 };
    if (xQueueSend(wl_ev_q, &it, 0) != pdTRUE)
        wl_trace(0x3f);                     /* lost: the queue was full */
}

static void bridge_event(int code)
{
    switch (code) {
        case CODE_WIFI_ON_MGMR_DONE:
            join_stored();
            break;
        case CODE_WIFI_ON_CONNECTING:
            if (want_link)
                set_state(WCP_STATE_JOINING);
            break;
        case CODE_WIFI_ON_CONNECTED:
            set_state(WCP_STATE_UP);
            /* the RSSI read at the connect event is 0 (the K230 logged "0 dBm"): send it again once beacons came */
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (state == WCP_STATE_UP)
                send_status();
            break;
        case CODE_WIFI_ON_DISCONNECT:
            if (own_disc > 0) {             /* the leave of a JOIN or LEAVE: already accounted for */
                own_disc--;
                break;
            }
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

/* the NetHub virtual-channel task: only queue (see struct wl_item) */
static int ctrl_rx(void *arg, uint8_t *data, uint16_t len)
{
    struct wl_item it = { .code = WL_CTRL };

    (void)arg;
    if (len < 1)
        return 0;
    it.len = len < sizeof(it.msg) ? len : sizeof(it.msg);
    memcpy(it.msg, data, it.len);
    if (xQueueSend(wl_ev_q, &it, 0) != pdTRUE)
        wl_trace(0x3e);                     /* lost: the worker's queue was full */
    return 0;
}

static void ctrl_handle(const uint8_t *data, uint16_t len)
{
    const struct wcp_join *j = (const struct wcp_join *)data;
    wifi_mgmr_scan_params_t sp;

    wl_trace(0x10 | (data[0] & 0x0f));
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
            if (state == WCP_STATE_UP || state == WCP_STATE_JOINING)
                own_disc++;                 /* its DISCONNECT event is not a loss for a later JOIN */
            wifi_mgmr_sta_autoconnect_disable();
            wifi_sta_disconnect();
            set_state(WCP_STATE_DOWN);
            break;
        case WCP_SCAN:
            /* one scan at a time: a request during a scan is answered by that scan's DONE (RT-Thread's join scans
             * right after a user's scan; a second wifi_mgmr_sta_scan() failed and sent a short list at once) */
            if (scan_pending)
                break;
            memset(&sp, 0, sizeof(sp));    /* all channels, active */
            scan_pending = true;
            if (wifi_mgmr_sta_scan(&sp) != 0)
                scan_done();               /* reports what the manager still has, maybe 0 */
            break;
        default:
            break;
    }
    wl_trace(0x1f);
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
    if (local_only)
        return NETHUB_WIFI_RX_FILTER_LOCAL;
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

bool wifi_link_claim(enum wifi_owner who)
{
    bool ok;

    taskENTER_CRITICAL();
    ok = owner == WIFI_OWNER_NONE || owner == who;
    if (ok)
        owner = who;
    taskEXIT_CRITICAL();
    return ok;
}

void wifi_link_release(enum wifi_owner who)
{
    taskENTER_CRITICAL();
    if (owner == who)
        owner = WIFI_OWNER_NONE;
    taskEXIT_CRITICAL();
}

/* Wi-Fi start as in the SDK's NetHub example, the rest follows from wifi_event(). Unlike there,
 * the BLE exchange memory is kept (no GLB_Set_EM_Sel(GLB_WRAM160KB_EM0KB)): BLE runs next to
 * Wi-Fi on USB power, as in examples/wifi/coex/wifi_ble */
static void start_wifi(void)
{
    wl_ev_q = xQueueCreate(WL_EV_DEPTH, sizeof(struct wl_item));
    wl_evg = xEventGroupCreate();
    xTaskCreate(wl_ev_task, "wl_ev", 1024, NULL, 10, NULL);   /* below the Wi-Fi tasks, above the app */
    async_register_event_filter(EV_WIFI, wifi_event, NULL);
    wifi_task_create();
    vTaskDelay(pdMS_TO_TICKS(500));
    fhost_init();
    wifi_up = true;
}

static void start_lwip(void)
{
    if (!lwip_up) {
        tcpip_init(NULL, NULL);
        lwip_up = true;
    }
}

int wifi_link_start(void)
{
    int ret;

    if (bridge_up) {
        ret = mr_sdio_drv_lowpower_restore();    /* K230 powered up again: re-mux and resync */
        sdio_pins_low_drive();
        if (rejoin) {
            rejoin = false;
            join_stored();
        }
        return ret;
    }

    start_lwip();

    /* NetHub first: the SDU must be answering by the time the K230's card detection runs */
    nethub_set_wifi_rx_filter(rx_filter, NULL);
    ret = nethub_bootstrap();
    if (ret != NETHUB_OK) {
        LOG_E("nethub bootstrap failed: %d\r\n", ret);
        return ret;
    }
    sdio_pins_low_drive();
    nethub_vchan_user_recv_register(ctrl_rx, NULL);
    bridge_up = true;

    if (!wifi_up)
        start_wifi();                /* joins from CODE_WIFI_ON_MGMR_DONE */
    else if (mgmr_ready)
        join_stored();               /* Wi-Fi came up earlier for a credential check */
    return 0;
}

void wifi_link_local_begin(void (*ev)(int code))
{
    local_only = true;
    local_ev = ev;
    start_lwip();
    if (!wifi_up)
        start_wifi();                /* ev gets CODE_WIFI_ON_MGMR_DONE */
    else if (mgmr_ready)
        ev(CODE_WIFI_ON_MGMR_DONE);
}

void wifi_link_local_end(void)
{
    local_ev = NULL;
    local_only = false;
    rejoin = bridge_up;          /* the check left the station disconnected */
}

void wifi_link_stop(void)
{
    /* SDU off and its pads released; k230_power_off() then parks them in analog mode */
    if (bridge_up)
        mr_sdio_drv_lowpower_prepare();
    /* the next K230 session starts unheard, like the first (see set_state()) */
    host_seen = false;
}
