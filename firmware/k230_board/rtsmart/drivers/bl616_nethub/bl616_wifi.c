/*
 * LEAKCAM BL616 Wi-Fi: RT-Thread WLAN station device on top of the NetHub link.
 *
 * The device registers as the station radio ("sta" -> lwIP netif "wlan0") once the BL616's
 * netdev channel has started, because its DEVICE_START carries the station MAC that the
 * netif must use. Frames go straight between lwIP and the netdev channel. Everything else is
 * the small control protocol of wifi_ctrl_proto.h on the virtual channel.
 *
 * The BL616 associates on its own with the credentials it keeps (set over BLE) as soon as it
 * powers the K230 up, so the link is usually up before RT-Smart has booted: the first
 * WCP_STATUS then reports UP and lwIP starts DHCP right away. rt_wlan_connect() ("wifi join"
 * in msh) still works and sends WCP_JOIN for this power-up only.
 *
 * Link changes are reported to the WLAN manager as they come from the BL616; the manager
 * accepts a CONNECT that it did not ask for.
 */
#include <rtthread.h>
#include <wlan_dev.h>
#include <wlan_mgnt.h>

#include "bl616_nethub.h"
#include "wifi_ctrl_proto.h"

#define DBG_TAG "bl616.wifi"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BL616_FRAME_MAX 1514   /* Ethernet header + 1500 MTU; the BL616 buffer holds no more */

static struct rt_wlan_device wlan;
static rt_bool_t registered;
static rt_uint8_t sta_mac[6];
static struct wcp_status status;   /* last one received, state starts DOWN */
static rt_bool_t told_up;          /* the WLAN manager has been told CONNECT */

static void report(rt_uint8_t was);

static rt_err_t send_cmd(rt_uint8_t type)
{
    struct wcp_cmd c = { type };

    return bl616_link_send_ctrl(&c, sizeof(c));
}

static rt_wlan_security_t security_of(rt_uint8_t auth)
{
    switch (auth)
    {
    case WCP_AUTH_OPEN:         return SECURITY_OPEN;
    case WCP_AUTH_WEP:          return SECURITY_WEP_PSK;
    case WCP_AUTH_WPA_PSK:      return SECURITY_WPA_AES_PSK;
    case WCP_AUTH_WPA2_PSK:     return SECURITY_WPA2_AES_PSK;
    case WCP_AUTH_WPA_WPA2_PSK: return SECURITY_WPA_WPA2_MIXED_PSK;
    case WCP_AUTH_WPA_ENT:      return SECURITY_WPA_WPA2_MIXED_8021X;
    case WCP_AUTH_WPA3_SAE:     return SECURITY_WPA3_SAE;
    case WCP_AUTH_WPA2_WPA3:    return SECURITY_WPA2_WPA3_MIXED_PSK;
    default:                    return SECURITY_UNKNOWN;
    }
}

/* ---------------------------------------------------------------- WLAN device operations */

static rt_err_t op_init(struct rt_wlan_device *dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_err_t op_mode(struct rt_wlan_device *dev, rt_wlan_mode_t mode)
{
    (void)dev;
    /* the BL616 firmware bridges its station only; NONE is what unregistering sets */
    return (mode == RT_WLAN_STATION || mode == RT_WLAN_NONE) ? RT_EOK : -RT_ENOSYS;
}

static rt_err_t op_scan(struct rt_wlan_device *dev, struct rt_scan_info *info)
{
    (void)dev;
    (void)info;    /* the BL616 always scans every channel; the manager filters the results */
    return send_cmd(WCP_SCAN);
}

static rt_err_t op_join(struct rt_wlan_device *dev, struct rt_sta_info *info)
{
    struct wcp_join j;

    (void)dev;
    if (!info->ssid.len || info->ssid.len > WCP_SSID_MAX || info->key.len > WCP_PSK_MAX)
        return -RT_EINVAL;
    rt_memset(&j, 0, sizeof(j));
    j.type = WCP_JOIN;
    j.ssid_len = info->ssid.len;
    j.psk_len = info->key.len;
    rt_memcpy(j.ssid, info->ssid.val, j.ssid_len);
    rt_memcpy(j.psk, info->key.val, j.psk_len);
    return bl616_link_send_ctrl(&j, sizeof(j));
}

static rt_err_t op_disconnect(struct rt_wlan_device *dev)
{
    (void)dev;
    return send_cmd(WCP_LEAVE);
}

static int op_get_rssi(struct rt_wlan_device *dev)
{
    (void)dev;
    /* the answer arrives later: this call returns the value from the last WCP_STATUS */
    send_cmd(WCP_GET_STATUS);
    return status.rssi;
}

static rt_err_t op_get_mac(struct rt_wlan_device *dev, rt_uint8_t mac[])
{
    (void)dev;
    rt_memcpy(mac, sta_mac, sizeof(sta_mac));
    return RT_EOK;
}

static int op_send(struct rt_wlan_device *dev, void *buff, int len)
{
    (void)dev;
    if (len <= 0 || len > BL616_FRAME_MAX)
        return -RT_EINVAL;
    return bl616_link_send_frame(buff, (rt_size_t)len);
}

static const struct rt_wlan_dev_ops bl616_wlan_ops = {
    .wlan_init = op_init,
    .wlan_mode = op_mode,
    .wlan_scan = op_scan,
    .wlan_join = op_join,
    .wlan_disconnect = op_disconnect,
    .wlan_get_rssi = op_get_rssi,
    .wlan_get_mac = op_get_mac,
    .wlan_send = op_send,
};

/* ---------------------------------------------------------------- from the link worker */

void bl616_wifi_attach(const rt_uint8_t mac[6])
{
    rt_err_t err;

    rt_memcpy(sta_mac, mac, sizeof(sta_mac));
    if (registered)
        return;
    err = rt_wlan_dev_register_auto(&wlan, "bl616", RT_WLAN_STATION, RT_WLAN_TRANSPORT_SDIO,
                                    &bl616_wlan_ops, RT_NULL);
    if (err != RT_EOK)
    {
        LOG_E("WLAN device registration failed: %d", (int)err);
        return;
    }
    /* op_send copies the frame into a transmit slot before it returns */
    wlan.flags |= RT_WLAN_FLAG_DIRECT_TX;
    registered = RT_TRUE;
    err = rt_wlan_set_mode(wlan.device.parent.name, RT_WLAN_STATION);
    if (err != RT_EOK)
        LOG_E("station mode failed: %d", (int)err);
    LOG_I("station %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
          mac[5]);
    /* a WCP_STATUS may have come in before the device existed */
    report(WCP_STATE_DOWN);
}

void bl616_wifi_detach(void)
{
    if (!registered)
        return;
    rt_wlan_set_mode(wlan.device.parent.name, RT_WLAN_NONE);
    rt_wlan_dev_unregister(&wlan);
    registered = RT_FALSE;
    told_up = RT_FALSE;
    /* a later attach must not report the old link as up: the BL616 sends a fresh STATUS */
    rt_memset(&status, 0, sizeof(status));
}

void bl616_wifi_ctrl_up(void)
{
    /* the BL616 may have associated long before this channel came up */
    send_cmd(WCP_GET_STATUS);
}

void bl616_wifi_rx(void *frame, rt_size_t len)
{
    if (registered)
        rt_wlan_dev_report_data(&wlan, frame, (int)len);
}

/* tell the WLAN manager what changed since the last report; `was` is the previous state */
static void report(rt_uint8_t was)
{
    if (!registered)
        return;
    if (status.state == WCP_STATE_UP && !told_up)
    {
        told_up = RT_TRUE;
        rt_wlan_dev_indicate_event_handle(&wlan, RT_WLAN_DEV_EVT_CONNECT, RT_NULL);
    }
    else if (status.state != WCP_STATE_UP && told_up)
    {
        told_up = RT_FALSE;
        rt_wlan_dev_indicate_event_handle(&wlan, RT_WLAN_DEV_EVT_DISCONNECT, RT_NULL);
    }
    else if (status.state == WCP_STATE_FAILED && was != WCP_STATE_FAILED)
    {
        rt_wlan_dev_indicate_event_handle(&wlan, RT_WLAN_DEV_EVT_CONNECT_FAIL, RT_NULL);
    }
}

static void on_status(const struct wcp_status *s)
{
    rt_uint8_t was = status.state;

    status = *s;
    if (status.ssid_len > WCP_SSID_MAX)
        status.ssid_len = WCP_SSID_MAX;
    if (status.state != was)
        LOG_I("link %u -> %u (%.*s, channel %u, %d dBm)", was, status.state, status.ssid_len,
              status.ssid, status.channel, status.rssi);
    report(was);
}

static void on_scan_item(const struct wcp_scan_item *it)
{
    struct rt_wlan_info info;
    struct rt_wlan_buff buff;
    rt_uint8_t n = it->ssid_len <= WCP_SSID_MAX ? it->ssid_len : WCP_SSID_MAX;

    rt_memset(&info, 0, sizeof(info));
    info.security = security_of(it->auth);
    info.band = RT_802_11_BAND_2_4GHZ;    /* the BL616 radio is 2.4 GHz only */
    info.channel = it->channel;
    info.rssi = it->rssi;
    info.ssid.len = n;
    rt_memcpy(info.ssid.val, it->ssid, n);
    rt_memcpy(info.bssid, it->bssid, sizeof(info.bssid));
    info.hidden = n == 0;
    buff.data = &info;
    buff.len = sizeof(info);
    rt_wlan_dev_indicate_event_handle(&wlan, RT_WLAN_DEV_EVT_SCAN_REPORT, &buff);
}

void bl616_wifi_ctrl_rx(const rt_uint8_t *msg, rt_size_t len)
{
    if (len < 1)
        return;
    switch (msg[0])
    {
    case WCP_STATUS:
        if (len >= sizeof(struct wcp_status))
            on_status((const struct wcp_status *)msg);
        break;
    case WCP_SCAN_ITEM:
        if (registered && len >= sizeof(struct wcp_scan_item))
            on_scan_item((const struct wcp_scan_item *)msg);
        break;
    case WCP_SCAN_DONE:
        if (registered)
            rt_wlan_dev_indicate_event_handle(&wlan, RT_WLAN_DEV_EVT_SCAN_DONE, RT_NULL);
        break;
    default:
        break;
    }
}
