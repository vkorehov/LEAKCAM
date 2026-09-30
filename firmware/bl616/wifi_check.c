/*
 * Credential check (wifi_check.h): scan for the SSID, join with DHCP, resolve and fetch
 * http://connectivitycheck.gstatic.com/generate_204, then leave. Runs on the BL616's own lwIP:
 * wifi_link_local_begin() keeps every received frame here and passes the Wi-Fi manager's events
 * to on_event().
 *
 * Not verified on hardware: which 802.11 codes the SDK leaves in its connect indication for a
 * wrong key (wchk_join_failure() reads the usual ones), and the timings.
 */
#include "wifi_check.h"

#include <errno.h>
#include <string.h>

#include <FreeRTOS.h>
#include <event_groups.h>
#include <task.h>

#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include "net_al.h"
#include "net_al_ext.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"

#include "wifi_link.h"

#define DBG_TAG "WCHK"
#include "log.h"

#define EV_READY        (1u << 0)
#define EV_SCANNED      (1u << 1)
#define EV_CONNECTED    (1u << 2)
#define EV_DISCONNECTED (1u << 3)
#define EV_GOT_IP       (1u << 4)
#define EV_IP_TIMEOUT   (1u << 5)

#define READY_MS   10000     /* Wi-Fi manager up (first use in this boot) */
#define SCAN_MS    10000
#define JOIN_MS    25000     /* association and WPA handshake */
#define DHCP_MS    20000
#define TCP_MS     10000     /* connect, then again for the answer */
#define LEAVE_MS   3000

static EventGroupHandle_t evg;

static void on_event(int code)
{
    EventBits_t b = 0;

    switch (code) {
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

struct seen {
    const char *ssid;
    size_t len;
    bool found;
};

static void scan_item(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
    struct seen *s = arg;

    (void)env;
    if (item->ssid_len >= 0 && (size_t)item->ssid_len == s->len && memcmp(item->ssid, s->ssid, s->len) == 0)
        s->found = true;
}

static bool ssid_seen(const char *ssid, size_t len)
{
    wifi_mgmr_scan_params_t sp;
    struct seen s = { ssid, len, false };

    memset(&sp, 0, sizeof(sp));
    sp.ssid_length = (uint8_t)len;         /* probe for it: hidden networks answer too */
    memcpy(sp.ssid_array, ssid, len);
    xEventGroupClearBits(evg, EV_SCANNED);
    if (wifi_mgmr_sta_scan(&sp) != 0 || !wait(EV_SCANNED, SCAN_MS))
        LOG_W("scan did not finish, using what the manager has\r\n");
    wifi_mgmr_scan_ap_all(NULL, &s, scan_item);
    return s.found;
}

/* TCP to the probe host with a deadline (lwIP's blocking connect waits out all SYN retries) */
static int probe_connect(const struct addrinfo *ai)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    lwip_fcntl(fd, F_SETFL, O_NONBLOCK);
    if (lwip_connect(fd, ai->ai_addr, ai->ai_addrlen) != 0 && errno != EINPROGRESS) {
        lwip_close(fd);
        return -1;
    }
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    struct timeval tv = { TCP_MS / 1000, 0 };
    int err = 0;
    socklen_t len = sizeof(err);
    if (lwip_select(fd + 1, NULL, &w, NULL, &tv) != 1 || lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ||
        err != 0) {
        lwip_close(fd);
        return -1;
    }
    lwip_fcntl(fd, F_SETFL, 0);
    struct timeval io = { TCP_MS / 1000, 0 };
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
    return fd;
}

static enum wifi_check_state internet(uint16_t *detail)
{
    static const char req[] = "GET " WCHK_PROBE_PATH " HTTP/1.1\r\nHost: " WCHK_PROBE_HOST
                              "\r\nConnection: close\r\n\r\n";
    struct addrinfo hints, *ai = NULL;
    char buf[64];
    int n = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (lwip_getaddrinfo(WCHK_PROBE_HOST, "80", &hints, &ai) != 0 || ai == NULL)
        return WCHK_NO_DNS;
    int fd = probe_connect(ai);
    lwip_freeaddrinfo(ai);
    if (fd < 0)
        return WCHK_NO_INTERNET;
    if (lwip_send(fd, req, sizeof(req) - 1, 0) == (int)sizeof(req) - 1) {
        int k;
        while (n < 12 && (k = lwip_recv(fd, buf + n, sizeof(buf) - (size_t)n, 0)) > 0)
            n += k;
    }
    lwip_close(fd);
    if (n == 0)
        return WCHK_NO_INTERNET;
    int status = wchk_http_status(buf, (size_t)n);
    if (status == 204)
        return WCHK_INTERNET;
    *detail = status < 0 ? 0 : (uint16_t)status;
    return WCHK_CAPTIVE;
}

static void leave(void)
{
    xEventGroupClearBits(evg, EV_DISCONNECTED);
    net_al_ext_dhcp_disconnect();          /* the lease must not outlive the check: the K230 */
    wifi_mgmr_sta_autoconnect_disable();   /* uses the same MAC for its own DHCP later */
    if (wifi_sta_disconnect() == 0)
        wait(EV_DISCONNECTED, LEAVE_MS);
}

enum wifi_check_state wifi_check_run(const char *ssid, size_t ssid_len, const char *psk, size_t psk_len,
                                     wifi_check_progress_cb progress, uint16_t *detail)
{
    wifi_mgmr_sta_connect_params_t p;
    enum wifi_check_state st;

    *detail = 0;
    if (evg == NULL)
        evg = xEventGroupCreate();
    xEventGroupClearBits(evg, 0xFF);
    progress(WCHK_JOINING, 0);
    wifi_link_local_begin(on_event);
    if (!wait(EV_READY, READY_MS)) {
        st = WCHK_JOIN_FAILED;
        goto out;
    }
    leave();                               /* a K230 session may have left the station joined */

    if (!ssid_seen(ssid, ssid_len)) {
        st = WCHK_NOT_FOUND;
        goto out;
    }
    memset(&p, 0, sizeof(p));
    memcpy(p.ssid, ssid, ssid_len);
    p.ssid_len = (uint8_t)ssid_len;
    memcpy(p.key, psk, psk_len);
    p.key_len = (uint8_t)psk_len;
    p.pmf_cfg = 1;
    p.use_dhcp = 1;                        /* no K230 running: the MAC is ours for now */
    p.dhcp_timeout_event_only = 1;         /* a DHCP timeout is a result, not a reason to rejoin */
    xEventGroupClearBits(evg, EV_CONNECTED | EV_DISCONNECTED | EV_GOT_IP | EV_IP_TIMEOUT);
    if (wifi_mgmr_sta_connect(&p) != 0) {
        st = WCHK_JOIN_FAILED;
        goto out;
    }
    EventBits_t b = wait(EV_CONNECTED | EV_DISCONNECTED, JOIN_MS);
    if (!(b & EV_CONNECTED)) {
        wifi_mgmr_connect_ind_stat_info_t info;
        memset(&info, 0, sizeof(info));
        wifi_mgmr_sta_connect_ind_stat_get(&info);
        st = wchk_join_failure(info.status_code, info.reason_code, detail);
        LOG_I("join failed: status %u reason %u\r\n", info.status_code, info.reason_code);
        goto out;
    }

    progress(WCHK_ADDRESS, 0);
    if (!(wait(EV_GOT_IP | EV_IP_TIMEOUT | EV_DISCONNECTED, DHCP_MS) & EV_GOT_IP)) {
        st = WCHK_NO_ADDRESS;
        goto out;
    }
    progress(WCHK_INTERNET, 0);
    st = internet(detail);

out:
    LOG_I("check of \"%.*s\": state 0x%02x detail %u\r\n", (int)ssid_len, ssid, st, *detail);
    leave();
    wifi_link_local_end();
    return st;
}
