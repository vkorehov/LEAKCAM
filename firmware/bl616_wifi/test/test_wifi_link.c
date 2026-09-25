/* Host test: BL616 wifi_link.c, built unchanged against stub/ (Bouffalo SDK, FreeRTOS, lwIP
 * stand-ins). The SDK calls are recorded here; the receive filter, the control-channel handler
 * and the Wi-Fi event handler are reached through the callbacks wifi_link_start() registers. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bl616_sdk_stub.h"
#include "wifi_ctrl_proto.h"
#include "wifi_link.h"

/* the wire format is shared with the K230 driver (packed, little-endian): pin it down */
_Static_assert(sizeof(struct wcp_cmd) == 1, "wcp_cmd");
_Static_assert(sizeof(struct wcp_join) == 99 && offsetof(struct wcp_join, ssid) == 3 &&
               offsetof(struct wcp_join, psk) == 35, "wcp_join");
_Static_assert(sizeof(struct wcp_status) == 43 && offsetof(struct wcp_status, bssid) == 4 &&
               offsetof(struct wcp_status, ssid_len) == 10 && offsetof(struct wcp_status, ssid) == 11,
               "wcp_status");
_Static_assert(sizeof(struct wcp_scan_item) == 43 && offsetof(struct wcp_scan_item, auth) == 3 &&
               offsetof(struct wcp_scan_item, bssid) == 4 && offsetof(struct wcp_scan_item, ssid) == 11,
               "wcp_scan_item");
_Static_assert(sizeof(struct wcp_scan_done) == 2, "wcp_scan_done");
/* the K230 driver's control slot (bl616_msg.c CTRL_SLOT_SIZE 128) holds the router (4), channel
 * (4) and vchan (4) headers plus the largest message */
_Static_assert(12 + sizeof(struct wcp_join) <= 128, "JOIN does not fit a K230 control slot");

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); \
                                      printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- recorded SDK */

static char calls[1024];            /* call order, space-separated names */
static void called(const char *n) { strncat(calls, n, sizeof(calls) - strlen(calls) - 2); strcat(calls, " "); }

static nethub_wifi_rx_filter_cb_t rx_filter;
static nethub_vchan_recv_cb_t vchan_rx;
static async_event_cb wifi_ev;
static int log_errors;

static struct { uint8_t b[128]; uint16_t len; } sent[64];
static unsigned nsent;
static int send_fail_at = -1;       /* nethub_vchan_user_send() fails for this message number */
static unsigned send_calls;

static wifi_mgmr_sta_connect_params_t conn;
static int connects, connect_result, disconnects, autoconnect = -1, scans, scan_result, mgmr_started;
static int lp_prepare, lp_restore, bootstraps;
static wifi_mgmr_scan_params_t scan_params;
static wifi_mgmr_scan_item_t scan_list[4];
static unsigned nscan_list;
static const char *stored_ssid, *stored_psk;
static struct { uint8_t pin; uint32_t cfg; } gpio[16];
static unsigned ngpio;

void test_log(char level, const char *fmt, ...)
{
    va_list ap;

    if (level == 'E')
        log_errors++;
    if (!getenv("WIFI_TEST_VERBOSE"))
        return;
    printf("  [%c] ", level);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void vTaskDelay(TickType_t t) { (void)t; }
void tcpip_init(void (*f)(void *), void *a) { (void)f; (void)a; called("tcpip"); }
int async_register_event_filter(uintptr_t type, async_event_cb cb, void *priv)
{
    (void)priv;
    CHECK(type == EV_WIFI, "event filter for type %lu", (unsigned long)type);
    wifi_ev = cb;
    called("events");
    return 0;
}
static struct bflb_device_s gpio_dev = { "gpio" };
struct bflb_device_s *bflb_device_get_by_name(const char *n) { return strcmp(n, "gpio") ? NULL : &gpio_dev; }
void bflb_gpio_init(struct bflb_device_s *dev, uint8_t pin, uint32_t cfg)
{
    CHECK(dev == &gpio_dev, "gpio device");
    if (ngpio < 16)
        gpio[ngpio].pin = pin, gpio[ngpio++].cfg = cfg;
}
void bflb_mtd_init(void) { called("mtd"); }
BL_Err_Type GLB_Set_EM_Sel(uint8_t em) { CHECK(em == GLB_WRAM160KB_EM0KB, "EM"); return 0; }
EfErrCode easyflash_init(void) { called("easyflash"); return 0; }
size_t ef_get_env_blob(const char *key, void *buf, size_t n, size_t *saved)
{
    const char *v = !strcmp(key, "wifi_ssid") ? stored_ssid : !strcmp(key, "wifi_psk") ? stored_psk : NULL;
    size_t l = v ? strlen(v) : 0;

    (void)saved;
    if (l > n)
        l = n;
    memcpy(buf, v ? v : "", l);
    return l;
}
int32_t rfparam_init(uint32_t a, void *p, uint32_t f) { (void)a; (void)p; (void)f; called("rf"); return 0; }
int fhost_init(void) { called("fhost"); return 0; }
void wifi_task_create(void) { called("wifi_task"); }
int wifi_mgmr_task_start(void) { mgmr_started++; return 0; }
int wifi_mgmr_sta_connect(const wifi_mgmr_sta_connect_params_t *c) { conn = *c; connects++; return connect_result; }
int wifi_sta_disconnect(void) { disconnects++; return 0; }
int wifi_mgmr_sta_rssi_get(int *rssi) { *rssi = -57; return 0; }
int wifi_mgmr_sta_connect_ind_stat_get(wifi_mgmr_connect_ind_stat_info_t *i)
{
    strcpy(i->ssid, "leak-net");
    memcpy(i->bssid, "\x10\x20\x30\x40\x50\x60", 6);
    i->channel = 11;
    return 0;
}
int wifi_mgmr_sta_scan(const wifi_mgmr_scan_params_t *c) { scan_params = *c; scans++; return scan_result; }
int wifi_mgmr_scan_ap_all(void *env, void *arg, scan_item_cb_t cb)
{
    for (unsigned i = 0; i < nscan_list; i++)
        cb(env, arg, &scan_list[i]);
    return 0;
}
int wifi_mgmr_sta_autoconnect_enable(void) { autoconnect = 1; return 0; }
int wifi_mgmr_sta_autoconnect_disable(void) { autoconnect = 0; return 0; }
int nethub_bootstrap(void) { bootstraps++; called("bootstrap"); return NETHUB_OK; }
int nethub_set_wifi_rx_filter(nethub_wifi_rx_filter_cb_t cb, void *ctx)
{
    (void)ctx;
    rx_filter = cb;
    called("rx_filter");
    return 0;
}
int nethub_vchan_user_send(const void *d, uint16_t len)
{
    if ((int)send_calls++ == send_fail_at)
        return -1;
    if (nsent < 64 && len <= sizeof(sent[0].b))
    {
        memcpy(sent[nsent].b, d, len);
        sent[nsent++].len = len;
    }
    return 0;
}
int nethub_vchan_user_recv_register(nethub_vchan_recv_cb_t cb, void *arg)
{
    (void)arg;
    vchan_rx = cb;
    called("vchan");
    return 0;
}
int mr_sdio_drv_lowpower_prepare(void) { lp_prepare++; return 0; }
int mr_sdio_drv_lowpower_restore(void) { lp_restore++; called("restore"); return 0; }

/* ---------------------------------------------------------------- helpers */

static void event(uint16_t code)
{
    struct async_input_event e = { EV_WIFI, 0, code, 0 };
    if (wifi_ev)
        wifi_ev(&e, NULL);
}

static void ctrl(const void *m, uint16_t len) { if (vchan_rx) vchan_rx(NULL, (uint8_t *)m, len); }
static void cmd(uint8_t type) { ctrl(&type, 1); }

static const struct wcp_status *last_status(void)
{
    return nsent && sent[nsent - 1].b[0] == WCP_STATUS ? (const struct wcp_status *)sent[nsent - 1].b : NULL;
}

/* exactly one new message since mark, a WCP_STATUS in this state */
static int one_status(unsigned mark, uint8_t state)
{
    return nsent == mark + 1 && last_status() && last_status()->state == state &&
           sent[mark].len == sizeof(struct wcp_status);
}

static nethub_wifi_rx_filter_action_t filter(uint16_t ethtype, uint16_t len)
{
    uint8_t f[64] = { 0 };
    struct pbuf p = { NULL, f, len, len };

    f[12] = (uint8_t)(ethtype >> 8);
    f[13] = (uint8_t)ethtype;
    if (!rx_filter)
        return (nethub_wifi_rx_filter_action_t)-1;
    return rx_filter(NETHUB_CHANNEL_WIFI_STA, &p, NULL);
}

static struct wcp_join mkjoin(uint8_t sl, uint8_t pl)
{
    struct wcp_join j;

    memset(&j, 0, sizeof(j));
    j.type = WCP_JOIN;
    j.ssid_len = sl;
    j.psk_len = pl;
    memset(j.ssid, 'S', sizeof(j.ssid));
    memset(j.psk, 'p', sizeof(j.psk));
    memcpy(j.ssid, "garage\x00x", 8);    /* an SSID may hold any byte, NUL included */
    return j;
}

/* ---------------------------------------------------------------- tests */

static void t_start(void)
{
    const uint8_t sdio_pins[] = { 10, 11, 12, 13, 14, 15 };

    CHECK(wifi_link_start() == 0, "start");
    /* NetHub's rule: the receive filter must be set before nethub_bootstrap() */
    CHECK(strstr(calls, "rx_filter bootstrap vchan events wifi_task fhost") != NULL, "start order: %s", calls);
    CHECK(strncmp(calls, "rf tcpip mtd easyflash", 22) == 0, "init order: %s", calls);
    CHECK(rx_filter && vchan_rx && wifi_ev, "callbacks not registered");
    CHECK(ngpio == 6, "%u SDIO pins configured", ngpio);
    for (unsigned i = 0; i < ngpio && i < 6; i++)
        CHECK(gpio[i].pin == sdio_pins[i] &&
              gpio[i].cfg == (GPIO_FUNC_SDU | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0),
              "pin %u cfg 0x%x", gpio[i].pin, (unsigned)gpio[i].cfg);
}

static void t_filter(void)
{
    CHECK(filter(0x888e, 60) == NETHUB_WIFI_RX_FILTER_LOCAL, "EAPOL not kept on the BL616");
    CHECK(filter(0x888e, 14) == NETHUB_WIFI_RX_FILTER_LOCAL, "minimal EAPOL header");
    CHECK(filter(0x0800, 60) == NETHUB_WIFI_RX_FILTER_HOST, "IPv4 (incl. DHCP, ICMP) not to the K230");
    CHECK(filter(0x0806, 60) == NETHUB_WIFI_RX_FILTER_HOST, "ARP not to the K230");
    CHECK(filter(0x86dd, 60) == NETHUB_WIFI_RX_FILTER_HOST, "IPv6 not to the K230");
    CHECK(filter(0x8e88, 60) == NETHUB_WIFI_RX_FILTER_HOST, "byte-swapped EAPOL kept");
    CHECK(filter(0x888e, 13) == NETHUB_WIFI_RX_FILTER_HOST, "runt frame read beyond its length");
}

static void t_boot_join(void)
{
    event(CODE_WIFI_ON_INIT_DONE);
    CHECK(mgmr_started == 1, "manager not started on INIT_DONE");
    event(CODE_WIFI_ON_MGMR_DONE);
    CHECK(connects == 0, "joined without stored credentials");

    stored_ssid = "leak-net";
    stored_psk = "0123456789";
    event(CODE_WIFI_ON_MGMR_DONE);
    CHECK(connects == 1 && conn.ssid_len == 8 && !memcmp(conn.ssid, "leak-net", 8) && conn.key_len == 10 &&
          !memcmp(conn.key, "0123456789", 10), "stored credentials not used");
    CHECK(conn.use_dhcp == 0 && conn.pmf_cfg == 1 && autoconnect == 1, "join options");
    event(CODE_WIFI_ON_CONNECTED);
    /* nobody listens before the K230's first message: nothing is queued for it */
    CHECK(nsent == 0, "status sent before the K230 spoke");

    cmd(WCP_GET_STATUS);
    const struct wcp_status *s = last_status();
    CHECK(one_status(0, WCP_STATE_UP), "GET_STATUS answer");
    CHECK(s && s->rssi == -57 && s->channel == 11 && s->ssid_len == 8 && !memcmp(s->ssid, "leak-net", 8) &&
          !memcmp(s->bssid, "\x10\x20\x30\x40\x50\x60", 6), "UP status fields");
}

static void t_link_events(void)
{
    unsigned m = nsent;

    event(CODE_WIFI_ON_DISCONNECT);    /* loss while up: reconnecting */
    CHECK(one_status(m, WCP_STATE_JOINING), "loss while UP");
    m = nsent;
    event(CODE_WIFI_ON_DISCONNECT);    /* loss while joining: attempt failed */
    CHECK(one_status(m, WCP_STATE_FAILED), "loss while JOINING");
    CHECK(last_status()->rssi == 0 && last_status()->channel == 0 && last_status()->ssid_len == 0,
          "not-UP status carries link data");
    m = nsent;
    event(CODE_WIFI_ON_CONNECTING);
    CHECK(one_status(m, WCP_STATE_JOINING), "CONNECTING");
    m = nsent;
    event(CODE_WIFI_ON_CONNECTING);
    CHECK(nsent == m, "unchanged state sent again");
    event(CODE_WIFI_ON_CONNECTED);
    CHECK(one_status(m, WCP_STATE_UP), "CONNECTED");
    m = nsent;
    event(CODE_WIFI_ON_GOT_IP);        /* the BL616 has no IP: ignored */
    CHECK(nsent == m, "GOT_IP acted on");

    cmd(WCP_LEAVE);
    CHECK(one_status(m, WCP_STATE_DOWN) && autoconnect == 0 && disconnects == 1, "LEAVE");
    m = nsent;
    event(CODE_WIFI_ON_DISCONNECT);
    event(CODE_WIFI_ON_CONNECTING);
    CHECK(nsent == m, "events after LEAVE changed the state");
}

static void t_join_cmd(void)
{
    struct wcp_join j = mkjoin(7, 12);
    unsigned m = nsent;
    int c = connects;

    ctrl(&j, sizeof(j));
    CHECK(connects == c + 1 && conn.ssid_len == 7 && !memcmp(conn.ssid, "garage\0", 7) && conn.key_len == 12 &&
          !memcmp(conn.key, "pppppppppppp", 12) && conn.use_dhcp == 0, "JOIN parameters");
    CHECK(one_status(m, WCP_STATE_JOINING) && autoconnect == 1, "JOIN status");

    j = mkjoin(32, 64);
    ctrl(&j, sizeof(j));
    CHECK(connects == c + 2 && conn.ssid_len == 32 && conn.key_len == 64, "longest JOIN");
    j = mkjoin(4, 0);                  /* open network */
    ctrl(&j, sizeof(j));
    CHECK(connects == c + 3 && conn.key_len == 0, "open JOIN");

    static const struct { uint8_t sl, pl; uint16_t len; } bad[] = {
        { 0, 8, 99 }, { 33, 8, 99 }, { 8, 65, 99 }, { 8, 8, 98 }, { 8, 8, 1 },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        event(CODE_WIFI_ON_CONNECTED);   /* from UP, so the FAILED is a change */
        m = nsent;
        j = mkjoin(bad[i].sl, bad[i].pl);
        ctrl(&j, bad[i].len);
        CHECK(connects == c + 3 && one_status(m, WCP_STATE_FAILED), "bad JOIN %u accepted", i);
    }

    event(CODE_WIFI_ON_CONNECTED);
    connect_result = -1;               /* the manager refuses the request */
    m = nsent;
    j = mkjoin(4, 8);
    ctrl(&j, sizeof(j));
    CHECK(nsent == m + 2 && sent[m].b[1] == WCP_STATE_JOINING && sent[m + 1].b[1] == WCP_STATE_FAILED &&
          log_errors > 0, "refused connect not reported FAILED");
    connect_result = 0;
}

static void t_scan(void)
{
    unsigned m = nsent;

    memset(scan_list, 0, sizeof(scan_list));
    for (unsigned i = 0; i < 3; i++)
    {
        scan_list[i].channel = (uint8_t)(1 + 5 * i);
        scan_list[i].rssi = (int8_t)(-40 - i);
        scan_list[i].auth = (uint8_t)(i == 2 ? WIFI_EVENT_BEACON_IND_AUTH_UNKNOWN : WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK + i);
        memcpy(scan_list[i].bssid, "\xaa\xbb\xcc\xdd\xee", 5);
        scan_list[i].bssid[5] = (uint8_t)i;
        memcpy(scan_list[i].ssid, "net-number-" "0123456789012345678901", 32);
        scan_list[i].ssid_len = i == 0 ? 6 : i == 1 ? 0 : 40;   /* hidden; longer than the array */
    }
    nscan_list = 3;

    event(CODE_WIFI_ON_SCAN_DONE);
    CHECK(nsent == m, "scan results sent without a WCP_SCAN");
    cmd(WCP_SCAN);
    CHECK(scans == 1 && scan_params.channels_cnt == 0 && !scan_params.passive && scan_params.ssid_length == 0,
          "scan must be all channels, active");
    CHECK(nsent == m, "results before the scan finished");
    event(CODE_WIFI_ON_SCAN_DONE);
    CHECK(nsent == m + 4, "%u messages for 3 APs", nsent - m);
    for (unsigned i = 0; i < 3 && m + i < nsent; i++)
    {
        const struct wcp_scan_item *it = (const struct wcp_scan_item *)sent[m + i].b;
        CHECK(sent[m + i].len == sizeof(*it) && it->type == WCP_SCAN_ITEM && it->channel == 1 + 5 * i &&
              it->rssi == -40 - (int)i && it->auth == scan_list[i].auth && it->bssid[5] == i &&
              !memcmp(it->bssid, "\xaa\xbb\xcc\xdd\xee", 5), "item %u fields", i);
        CHECK(it->ssid_len == (i == 0 ? 6 : i == 1 ? 0 : 32) && !memcmp(it->ssid, scan_list[i].ssid, it->ssid_len),
              "item %u SSID length %u", i, it->ssid_len);
    }
    CHECK(sent[m + 3].len == 2 && sent[m + 3].b[0] == WCP_SCAN_DONE && sent[m + 3].b[1] == 3, "SCAN_DONE count");
    m = nsent;
    event(CODE_WIFI_ON_SCAN_DONE);
    CHECK(nsent == m, "second SCAN_DONE event answered");

    /* a refused scan answers at once with what the manager has; a lost item is not counted */
    scan_result = -1;
    send_fail_at = (int)send_calls + 1;
    cmd(WCP_SCAN);
    CHECK(nsent == m + 3 && sent[m + 2].b[0] == WCP_SCAN_DONE && sent[m + 2].b[1] == 2,
          "refused scan / lost item: %u messages, count %u", nsent - m, nsent > m + 2 ? sent[m + 2].b[1] : 0);
    send_fail_at = -1;
    scan_result = 0;
}

static void t_odd(void)
{
    unsigned m = nsent;
    uint8_t x = 0x7f;

    ctrl(&x, 0);
    ctrl(&x, 1);
    cmd(WCP_STATUS);                   /* a BL616 -> K230 type coming the wrong way */
    CHECK(nsent == m, "unknown or empty control message answered");
}

static void t_restart(void)
{
    ngpio = 0;
    calls[0] = 0;
    wifi_link_stop();
    CHECK(lp_prepare == 1, "stop did not release the SDU");
    CHECK(wifi_link_start() == 0 && lp_restore == 1 && bootstraps == 1, "restart re-initialised NetHub");
    CHECK(!strcmp(calls, "restore "), "restart did more than restore: %s", calls);
    CHECK(ngpio == 6 && (gpio[0].cfg & GPIO_DRV_MASK) == GPIO_DRV_0, "SDIO pins not back at drive 0");

    /* the K230 goes off and on again while the association is kept: link changes in between
     * have no listener, and the new session learns the state from its own GET_STATUS */
    unsigned m = nsent;
    wifi_link_stop();
    event(CODE_WIFI_ON_CONNECTED);
    event(CODE_WIFI_ON_DISCONNECT);
    event(CODE_WIFI_ON_CONNECTED);
    CHECK(nsent == m, "%u status messages queued while the K230 was off", nsent - m);
    CHECK(wifi_link_start() == 0, "restart");
    cmd(WCP_GET_STATUS);
    CHECK(one_status(nsent - 1, WCP_STATE_UP), "first GET_STATUS of the new session");
    m = nsent;
    event(CODE_WIFI_ON_DISCONNECT);
    CHECK(one_status(m, WCP_STATE_JOINING), "changes reported again once the K230 spoke");
}

int main(void)
{
    t_start();
    t_filter();
    t_boot_join();
    t_link_events();
    t_join_cmd();
    t_scan();
    t_odd();
    t_restart();
    if (fails)
        printf("wifi_link: FAIL (%d)\n", fails);
    else
        printf("wifi_link: receive filter, start order, stored join, link states, JOIN/LEAVE/SCAN, "
               "protocol layout all pass\n");
    return fails != 0;
}
