/*
 * LEAKCAM Wi-Fi control messages between the K230 (RT-Smart driver bl616_nethub) and the BL616.
 *
 * They ride on NetHub's USER virtual channel, so they share the SDIO link with the Ethernet
 * frames but never with the IP stack. The BL616 is only the radio and the Ethernet bridge:
 * DHCP, ARP, ICMP, DNS and every socket run on the K230's lwIP, with the BL616's station MAC.
 *
 * One message per virtual-channel packet, little-endian, packed, first byte = type.
 *
 * K230 -> BL616
 *   WCP_GET_STATUS   ask for a WCP_STATUS now (the K230 sends it once the channel is up)
 *   WCP_JOIN         associate with ssid/psk for this power-up (not stored: the stored
 *                    credentials come over BLE and are the BL616's own business)
 *   WCP_LEAVE        disassociate and stop reconnecting until the next WCP_JOIN
 *   WCP_SCAN         scan all channels; answered by WCP_SCAN_ITEM x n, then WCP_SCAN_DONE
 * BL616 -> K230
 *   WCP_STATUS       reply to WCP_GET_STATUS, and unsolicited on every link change
 *   WCP_SCAN_ITEM    one access point
 *   WCP_SCAN_DONE    end of a scan, with the number of items sent
 *
 * This header is the only definition of the format; install.sh copies it next to the K230
 * driver sources.
 */
#ifndef LEAKCAM_WIFI_CTRL_PROTO_H
#define LEAKCAM_WIFI_CTRL_PROTO_H

#include <stdint.h>

#define WCP_SSID_MAX 32
#define WCP_PSK_MAX  64

enum wcp_type {
    WCP_GET_STATUS = 0x01,
    WCP_JOIN       = 0x02,
    WCP_LEAVE      = 0x03,
    WCP_SCAN       = 0x04,

    WCP_STATUS     = 0x81,
    WCP_SCAN_ITEM  = 0x82,
    WCP_SCAN_DONE  = 0x83,
};

enum wcp_state {
    WCP_STATE_DOWN    = 0,  /* not associated, not trying (no credentials, or after WCP_LEAVE) */
    WCP_STATE_JOINING = 1,  /* association or WPA handshake in progress, or reconnecting */
    WCP_STATE_UP      = 2,  /* associated and keys installed: frames flow */
    WCP_STATE_FAILED  = 3,  /* the last join attempt failed; the BL616 keeps retrying */
};

/* same numbers as the BL616 SDK's WIFI_EVENT_BEACON_IND_AUTH_* */
enum wcp_auth {
    WCP_AUTH_OPEN          = 0,
    WCP_AUTH_WEP           = 1,
    WCP_AUTH_WPA_PSK       = 2,
    WCP_AUTH_WPA2_PSK      = 3,
    WCP_AUTH_WPA_WPA2_PSK  = 4,
    WCP_AUTH_WPA_ENT       = 5,
    WCP_AUTH_WPA3_SAE      = 6,
    WCP_AUTH_WPA2_WPA3     = 7,
    WCP_AUTH_UNKNOWN       = 0xff,
};

#pragma pack(push, 1)

/* WCP_GET_STATUS, WCP_LEAVE, WCP_SCAN */
struct wcp_cmd {
    uint8_t type;
};

struct wcp_join {
    uint8_t type;                  /* WCP_JOIN */
    uint8_t ssid_len;              /* 1..32 */
    uint8_t psk_len;               /* 0 (open) or 8..64 */
    char ssid[WCP_SSID_MAX];       /* not NUL-terminated: an SSID may hold any byte */
    char psk[WCP_PSK_MAX];
};

struct wcp_status {
    uint8_t type;                  /* WCP_STATUS */
    uint8_t state;                 /* enum wcp_state */
    int8_t rssi;                   /* dBm, 0 when not UP */
    uint8_t channel;               /* 0 when not UP */
    uint8_t bssid[6];
    uint8_t ssid_len;
    char ssid[WCP_SSID_MAX];
};

struct wcp_scan_item {
    uint8_t type;                  /* WCP_SCAN_ITEM */
    uint8_t channel;
    int8_t rssi;
    uint8_t auth;                  /* enum wcp_auth */
    uint8_t bssid[6];
    uint8_t ssid_len;              /* 0 = hidden */
    char ssid[WCP_SSID_MAX];
};

struct wcp_scan_done {
    uint8_t type;                  /* WCP_SCAN_DONE */
    uint8_t count;                 /* WCP_SCAN_ITEMs sent for this scan */
};

#pragma pack(pop)

#endif
