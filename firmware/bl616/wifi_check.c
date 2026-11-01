/*
 * Credential check (wifi_check.h): scan for the SSID, join with DHCP, resolve and fetch
 * http://connectivitycheck.gstatic.com/generate_204, then leave. Runs on the BL616's own lwIP:
 * wifi_link_local_begin() keeps every received frame here and passes the Wi-Fi manager's events
 * to on_event().
 *
 * A wrong WPA2 key on hardware (tests/test106, 2026-10-30): the connect indication holds 0/0, the
 * SDK's firmware status 33 (no EAPOL 3/4) tells it: wchk_join_failure() reads all three.
 */
#include "wifi_check.h"

#include <errno.h>
#include <stdio.h>
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
#define FAST_TU    40        /* per channel: the probed AP answers in ms; the SDK's 220 TU took 2.9 s (test106) */
#define JOIN_MS    25000     /* association and WPA handshake */
#define DHCP_MS    20000
#define TCP_MS     10000     /* connect, then again for the answer */

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
        (void)xEventGroupSetBits(evg, b);   /* returns the bits, not an error */
}

static EventBits_t wait(EventBits_t bits, uint32_t ms)
{
    return xEventGroupWaitBits(evg, bits, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)) & bits;
}

struct seen {
    const char *ssid;
    size_t len;
    bool found;
    int8_t rssi;                           /* the strongest AP with the SSID: the join goes to it */
    uint8_t channel;
    uint8_t bssid[6];
};

static void scan_item(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
    struct seen *s = arg;

    (void)env;
    if (item->ssid_len >= 0 && (size_t)item->ssid_len == s->len && memcmp(item->ssid, s->ssid, s->len) == 0 &&
        (!s->found || item->rssi > s->rssi)) {
        s->found = true;
        s->rssi = item->rssi;
        s->channel = item->channel;
        memcpy(s->bssid, item->bssid, sizeof(s->bssid));
    }
}

static bool ssid_seen(struct seen *s, uint32_t dwell_tu)
{
    wifi_mgmr_scan_params_t sp;

    memset(&sp, 0, sizeof(sp));
    sp.ssid_length = (uint8_t)s->len;      /* probe for it: hidden networks answer too */
    memcpy(sp.ssid_array, s->ssid, s->len);
    sp.duration = dwell_tu;                /* 0: the SDK's default */
    (void)xEventGroupClearBits(evg, EV_SCANNED);   /* the old bits, not an error */
    if (wifi_mgmr_sta_scan(&sp) != 0 || !wait(EV_SCANNED, SCAN_MS))
        LOG_W("scan did not finish, using what the manager has\r\n");
    (void)wifi_mgmr_scan_ap_all(NULL, s, scan_item);   /* always 0 (wifi_mgmr_ext.h) */
    return s->found;
}

/* TCP to the probe host with a deadline (lwIP's blocking connect waits out all SYN retries) */
static int probe_connect(const struct addrinfo *ai)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    /* blocking, the connect would wait out the SYN retries instead of TCP_MS */
    if (lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        (lwip_connect(fd, ai->ai_addr, ai->ai_addrlen) != 0 && errno != EINPROGRESS)) {
        (void)lwip_close(fd);           /* dropped: nothing to do on a close error */
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
        (void)lwip_close(fd);           /* dropped: nothing to do on a close error */
        return -1;
    }
    /* without them a recv would return at once (still non-blocking) or never (no timeout) */
    struct timeval io = { TCP_MS / 1000, 0 };
    if (lwip_fcntl(fd, F_SETFL, 0) != 0 || lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io)) != 0 ||
        lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io)) != 0) {
        (void)lwip_close(fd);           /* dropped: nothing to do on a close error */
        return -1;
    }
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
    (void)lwip_close(fd);               /* the answer is read: a close error changes nothing */
    if (n == 0)
        return WCHK_NO_INTERNET;
    int status = wchk_http_status(buf, (size_t)n);
    if (status == 204)
        return WCHK_INTERNET;
    *detail = status < 0 ? 0 : (uint16_t)status;
    return WCHK_CAPTIVE;
}

/* false if the station stayed busy (wifi_link_sta_leave()): no connect then, it would never return */
static bool leave(void)
{
    net_al_ext_dhcp_disconnect();          /* the lease must not outlive the check: the K230 uses the same MAC */
    return wifi_link_sta_leave();          /* for its own DHCP later */
}

/* the GOT_IP event, or the address itself: for a second check in the same boot the SDK bound the lease and sent no
 * GOT_IP, and the check waited out DHCP_MS to report "no address" (tests/test106, 2026-10-30). The address is 0 here
 * until this join's DHCP: the last check's leave released it */
static bool got_address(void)
{
    uint32_t ip, mask, gw, dns;

    for (uint32_t ms = 0; ms < DHCP_MS; ms += 50) {
        EventBits_t b = wait(EV_GOT_IP | EV_IP_TIMEOUT | EV_DISCONNECTED, 50);
        if (b)
            return b & EV_GOT_IP;
        if (wifi_sta_ip4_addr_get(&ip, &mask, &gw, &dns) == 0 && ip != 0)
            return true;
    }
    return false;
}

enum wifi_check_state wifi_check_run(const char *ssid, size_t ssid_len, const char *psk, size_t psk_len,
                                     wifi_check_progress_cb progress, uint16_t *detail)
{
    wifi_mgmr_sta_connect_params_t p;
    enum wifi_check_state st;

    *detail = 0;
    if (evg == NULL && (evg = xEventGroupCreate()) == NULL)
        vAssertCalled();
    (void)xEventGroupClearBits(evg, 0xFF);   /* the old bits, not an error */
    progress(WCHK_JOINING, 0);
    wifi_link_local_begin(on_event);
    if (!wait(EV_READY, READY_MS)) {
        st = WCHK_JOIN_FAILED;
        goto out;
    }
    if (!leave()) {                        /* a K230 session may have left it joined, or retrying a network */
        st = WCHK_BUSY;
        goto out;
    }

    struct seen s = { ssid, ssid_len, false, 0, 0, { 0 } };
    if (!ssid_seen(&s, FAST_TU) && !ssid_seen(&s, 0)) {   /* the full dwell before "not seen" */
        st = WCHK_NOT_FOUND;
        goto out;
    }
    memset(&p, 0, sizeof(p));
    memcpy(p.ssid, ssid, ssid_len);
    p.ssid_len = (uint8_t)ssid_len;
    memcpy(p.key, psk, psk_len);
    p.key_len = (uint8_t)psk_len;
    p.pmf_cfg = 1;
    p.use_dhcp = 1;                        /* the MAC is ours for now: a K230 session waits (wifi_link.c) */
    p.dhcp_timeout_event_only = 1;         /* a DHCP timeout is a result, not a reason to rejoin */
    /* the AP this scan found, as wifi_link.c's remembered join: without it the supplicant scanned every channel a
     * second time, 2.9 s of the user's wait (tests/test106, 2026-10-30) */
    p.freq1 = (uint16_t)(s.channel == 14 ? 2484 : 2407 + 5 * s.channel);
    snprintf(p.bssid_str, sizeof(p.bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X", s.bssid[0], s.bssid[1], s.bssid[2],
             s.bssid[3], s.bssid[4], s.bssid[5]);   /* the SDK wants it upper case */
    p.quick_connect = 1;
    (void)xEventGroupClearBits(evg, EV_CONNECTED | EV_DISCONNECTED | EV_GOT_IP | EV_IP_TIMEOUT);   /* the old bits */
    if (wifi_mgmr_sta_connect(&p) != 0) {
        st = WCHK_JOIN_FAILED;
        goto out;
    }
    EventBits_t b = wait(EV_CONNECTED | EV_DISCONNECTED, JOIN_MS);
    if (!(b & EV_CONNECTED)) {
        wifi_mgmr_connect_ind_stat_info_t info;
        memset(&info, 0, sizeof(info));
        (void)wifi_mgmr_sta_connect_ind_stat_get(&info);   /* zeroed above: a failed read gives 0/0 */
        uint16_t fw = wifi_mgmr_sta_info_status_code_get();
        st = wchk_join_failure(fw, info.status_code, info.reason_code, detail);
        LOG_I("join failed: fw %u status %u reason %u\r\n", fw, info.status_code, info.reason_code);
        goto out;
    }

    progress(WCHK_ADDRESS, 0);
    if (!got_address()) {
        st = WCHK_NO_ADDRESS;
        goto out;
    }
    progress(WCHK_INTERNET, 0);
    st = internet(detail);

out:
    LOG_I("check of \"%.*s\": state 0x%02x detail %u\r\n", (int)ssid_len, ssid, st, *detail);
    if (!leave())                          /* the next check answers WCHK_BUSY, a K230 join reports FAILED */
        LOG_W("station stayed busy after the check\r\n");
    wifi_link_local_end();
    return st;
}
