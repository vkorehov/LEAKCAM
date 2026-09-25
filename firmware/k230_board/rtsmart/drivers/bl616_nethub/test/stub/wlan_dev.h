/* Host stand-in for the RT-Thread WLAN device API used by bl616_wifi.c: the enum values and
 * structure members it touches, as in components/drivers/wlan/wlan_dev.h of the k230_rtos_sdk. */
#ifndef TEST_STUB_WLAN_DEV_H
#define TEST_STUB_WLAN_DEV_H

#include <rtthread.h>

typedef enum { RT_WLAN_NONE, RT_WLAN_STATION, RT_WLAN_AP, RT_WLAN_MODE_MAX } rt_wlan_mode_t;

typedef enum
{
    RT_WLAN_TRANSPORT_UNKNOWN = 0,
    RT_WLAN_TRANSPORT_USB,
    RT_WLAN_TRANSPORT_SDIO,
    RT_WLAN_TRANSPORT_SPI,
} rt_wlan_transport_t;

typedef enum
{
    RT_WLAN_DEV_EVT_INIT_DONE = 0,
    RT_WLAN_DEV_EVT_CONNECT,
    RT_WLAN_DEV_EVT_CONNECT_FAIL,
    RT_WLAN_DEV_EVT_DISCONNECT,
    RT_WLAN_DEV_EVT_AP_START,
    RT_WLAN_DEV_EVT_AP_STOP,
    RT_WLAN_DEV_EVT_AP_ASSOCIATED,
    RT_WLAN_DEV_EVT_AP_DISASSOCIATED,
    RT_WLAN_DEV_EVT_AP_ASSOCIATE_FAILED,
    RT_WLAN_DEV_EVT_SCAN_REPORT,
    RT_WLAN_DEV_EVT_SCAN_DONE,
    RT_WLAN_DEV_EVT_MAX,
} rt_wlan_dev_event_t;

#define WEP_ENABLED        0x0001
#define TKIP_ENABLED       0x0002
#define AES_ENABLED        0x0004
#define SAE_ENABLED        0x0020
#define WPA_SECURITY       0x00200000
#define WPA2_SECURITY      0x00400000
#define WPA3_SECURITY      0x00800000
#define IEEE_8021X_ENABLED 0x80000000

#define RT_WLAN_FLAG_DIRECT_TX (0x1 << 2)

#define RT_WLAN_SSID_MAX_LENGTH     32
#define RT_WLAN_BSSID_MAX_LENGTH    6
#define RT_WLAN_PASSWORD_MAX_LENGTH 64

typedef enum
{
    SECURITY_OPEN                 = 0,
    SECURITY_WEP_PSK              = WEP_ENABLED,
    SECURITY_WPA_AES_PSK          = (WPA_SECURITY | AES_ENABLED),
    SECURITY_WPA2_AES_PSK         = (WPA2_SECURITY | AES_ENABLED),
    SECURITY_WPA_WPA2_MIXED_PSK   = (WPA_SECURITY | WPA2_SECURITY),
    SECURITY_WPA_WPA2_MIXED_8021X = (int)(IEEE_8021X_ENABLED | WPA_SECURITY | WPA2_SECURITY),
    SECURITY_WPA3_SAE             = (WPA3_SECURITY | AES_ENABLED | SAE_ENABLED),
    SECURITY_WPA2_WPA3_MIXED_PSK  = (WPA2_SECURITY | WPA3_SECURITY | AES_ENABLED | SAE_ENABLED),
    SECURITY_UNKNOWN              = -1,
} rt_wlan_security_t;

typedef enum
{
    RT_802_11_BAND_5GHZ = 0,
    RT_802_11_BAND_2_4GHZ = 1,
    RT_802_11_BAND_UNKNOWN = 0x7fffffff,
} rt_802_11_band_t;

struct rt_wlan_ssid { rt_uint8_t len; rt_uint8_t val[RT_WLAN_SSID_MAX_LENGTH + 1]; };
struct rt_wlan_key { rt_uint8_t len; rt_uint8_t val[RT_WLAN_PASSWORD_MAX_LENGTH + 1]; };
typedef struct rt_wlan_ssid rt_wlan_ssid_t;
typedef struct rt_wlan_key rt_wlan_key_t;

struct rt_wlan_info
{
    rt_wlan_security_t security;
    rt_802_11_band_t band;
    rt_uint32_t datarate;
    rt_int16_t channel;
    rt_int16_t rssi;
    rt_wlan_ssid_t ssid;
    rt_uint8_t bssid[RT_WLAN_BSSID_MAX_LENGTH];
    rt_uint8_t hidden;
};

struct rt_wlan_buff { void *data; rt_int32_t len; };

struct rt_wlan_dev_ops;

struct rt_wlan_device
{
    struct rt_device device;
    const struct rt_wlan_dev_ops *ops;
    rt_uint32_t flags;
    void *user_data;
};

struct rt_sta_info
{
    rt_wlan_ssid_t ssid;
    rt_wlan_key_t key;
    rt_uint8_t bssid[6];
    rt_uint16_t channel;
    rt_802_11_band_t band;
    rt_wlan_security_t security;
};

struct rt_scan_info
{
    rt_wlan_ssid_t ssid;
    rt_uint8_t bssid[6];
    rt_int16_t channel_min;
    rt_int16_t channel_max;
    rt_802_11_band_t band;
    rt_bool_t band_locked;
    rt_bool_t passive;
};

struct rt_wlan_dev_ops
{
    rt_err_t (*wlan_init)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_mode)(struct rt_wlan_device *wlan, rt_wlan_mode_t mode);
    rt_err_t (*wlan_scan)(struct rt_wlan_device *wlan, struct rt_scan_info *scan_info);
    rt_err_t (*wlan_join)(struct rt_wlan_device *wlan, struct rt_sta_info *sta_info);
    rt_err_t (*wlan_softap)(struct rt_wlan_device *wlan, void *ap_info);
    rt_err_t (*wlan_disconnect)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_ap_stop)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_ap_deauth)(struct rt_wlan_device *wlan, rt_uint8_t mac[]);
    rt_err_t (*wlan_scan_stop)(struct rt_wlan_device *wlan);
    int (*wlan_get_rssi)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_set_powersave)(struct rt_wlan_device *wlan, int level);
    int (*wlan_get_powersave)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_cfg_promisc)(struct rt_wlan_device *wlan, rt_bool_t start);
    rt_err_t (*wlan_cfg_filter)(struct rt_wlan_device *wlan, void *filter);
    rt_err_t (*wlan_cfg_mgnt_filter)(struct rt_wlan_device *wlan, rt_bool_t start);
    rt_err_t (*wlan_set_channel)(struct rt_wlan_device *wlan, int channel);
    int (*wlan_get_channel)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_set_country)(struct rt_wlan_device *wlan, int country_code);
    int (*wlan_get_country)(struct rt_wlan_device *wlan);
    rt_err_t (*wlan_set_mac)(struct rt_wlan_device *wlan, rt_uint8_t mac[]);
    rt_err_t (*wlan_get_mac)(struct rt_wlan_device *wlan, rt_uint8_t mac[]);
    int (*wlan_recv)(struct rt_wlan_device *wlan, void *buff, int len);
    int (*wlan_send)(struct rt_wlan_device *wlan, void *buff, int len);
    int (*wlan_send_raw_frame)(struct rt_wlan_device *wlan, void *buff, int len);
};

void rt_wlan_dev_indicate_event_handle(struct rt_wlan_device *device, rt_wlan_dev_event_t event,
                                       struct rt_wlan_buff *buff);
rt_err_t rt_wlan_dev_report_data(struct rt_wlan_device *device, void *buff, int len);
rt_err_t rt_wlan_dev_register_auto(struct rt_wlan_device *wlan, const char *model_name,
                                   rt_wlan_mode_t mode, rt_wlan_transport_t transport,
                                   const struct rt_wlan_dev_ops *ops, void *user_data);
rt_err_t rt_wlan_dev_unregister(struct rt_wlan_device *wlan);

#endif
