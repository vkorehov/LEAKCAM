/*
 * Host stand-in for the Bouffalo SDK, FreeRTOS and lwIP pieces wifi_link.c uses: the types,
 * constants and prototypes as in bouffalo_sdk (wifi6 fhost, nethub, lhal) and lwIP. Every SDK
 * header wifi_link.c includes is a one-line file here that includes this one. The functions
 * are implemented by test_wifi_link.c, which records the calls.
 */
#ifndef TEST_BL616_SDK_STUB_H
#define TEST_BL616_SDK_STUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- FreeRTOS */
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
void vTaskDelay(TickType_t ticks);

/* ---- lwIP */
typedef uint8_t u8_t;
typedef uint16_t u16_t;
struct pbuf { struct pbuf *next; void *payload; u16_t tot_len; u16_t len; };
struct eth_addr { u8_t addr[6]; } __attribute__((packed));
struct eth_hdr { struct eth_addr dest; struct eth_addr src; u16_t type; } __attribute__((packed));
#define SIZEOF_ETH_HDR 14
#define PP_HTONS(x) ((u16_t)((((x) & 0x00ffU) << 8) | (((x) & 0xff00U) >> 8)))
void tcpip_init(void (*initfunc)(void *), void *arg);

/* ---- async_event */
typedef struct async_input_event {
    uintptr_t type;
    uint16_t size;
    uint16_t code;
    unsigned long value;
} *async_input_event_t;
typedef void (*async_event_cb)(async_input_event_t event, void *private_data);
int async_register_event_filter(uintptr_t type, async_event_cb cb, void *priv);

/* ---- lhal GPIO (BL616 values) */
struct bflb_device_s { const char *name; };
struct bflb_device_s *bflb_device_get_by_name(const char *name);
void bflb_gpio_init(struct bflb_device_s *dev, uint8_t pin, uint32_t cfgset);
#define GPIO_PIN_0  0
#define GPIO_PIN_1  1
#define GPIO_PIN_3  3
#define GPIO_PIN_10 10
#define GPIO_PIN_11 11
#define GPIO_PIN_12 12
#define GPIO_PIN_13 13
#define GPIO_PIN_14 14
#define GPIO_PIN_15 15
#define GPIO_PIN_20 20
#define GPIO_PIN_21 21
#define GPIO_PIN_22 22
#define GPIO_PIN_28 28
#define GPIO_PIN_29 29
#define GPIO_PIN_30 30
#define GPIO_FUNC_SDU  (12 << 0)
#define GPIO_ALTERNATE (1 << 8)
#define GPIO_PULLUP    (1 << 9)
#define GPIO_SMT_EN    (1 << 11)
#define GPIO_DRV_MASK  (3 << 12)
#define GPIO_DRV_0     (0 << 12)
#define GPIO_DRV_1     (1 << 12)

/* ---- system */
void bflb_mtd_init(void);
typedef int BL_Err_Type;
#define GLB_WRAM160KB_EM0KB 0
BL_Err_Type GLB_Set_EM_Sel(uint8_t emType);
typedef int EfErrCode;
EfErrCode easyflash_init(void);
size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len);
int32_t rfparam_init(uint32_t base_addr, void *rf_para, uint32_t apply_flag);
int fhost_init(void);
void wifi_task_create(void);

/* ---- Wi-Fi manager (wifi6 fhost wifi_mgmr_ext.h) */
#define MGMR_SSID_LEN  32
#define MGMR_KEY_LEN   64
#define MGMR_BSSID_LEN 18
#define MGMR_AKM_LEN   15

#define EV_WIFI                0x0002
#define CODE_WIFI_ON_INIT_DONE 1
#define CODE_WIFI_ON_MGMR_DONE 2
#define CODE_WIFI_ON_CONNECTED 4
#define CODE_WIFI_ON_DISCONNECT 5
#define CODE_WIFI_ON_GOT_IP    7
#define CODE_WIFI_ON_CONNECTING 8
#define CODE_WIFI_ON_SCAN_DONE 9

#define WIFI_EVENT_BEACON_IND_AUTH_OPEN              0
#define WIFI_EVENT_BEACON_IND_AUTH_WEP               1
#define WIFI_EVENT_BEACON_IND_AUTH_WPA_PSK           2
#define WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK          3
#define WIFI_EVENT_BEACON_IND_AUTH_WPA_WPA2_PSK      4
#define WIFI_EVENT_BEACON_IND_AUTH_WPA_ENT           5
#define WIFI_EVENT_BEACON_IND_AUTH_WPA3_SAE          6
#define WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK_WPA3_SAE 7
#define WIFI_EVENT_BEACON_IND_AUTH_UNKNOWN           0xff

typedef struct wifi_mgmr_scan_item {
    uint32_t mode;
    uint32_t timestamp_lastseen;
    int ssid_len;
    uint8_t channel;
    int8_t rssi;
    char ssid[32];
    char ssid_tail[1];
    uint8_t bssid[6];
    uint8_t auth;
} wifi_mgmr_scan_item_t;

typedef struct wifi_mgmr_sta_connect_params {
    char ssid[MGMR_SSID_LEN];
    uint8_t ssid_tail[1];
    char key[MGMR_KEY_LEN];
    char key_tail[1];
    uint8_t ssid_len;
    uint8_t key_len;
    char bssid_str[MGMR_BSSID_LEN];
    char akm_str[MGMR_AKM_LEN];
    uint8_t akm_len;
    uint16_t freq1;
    uint16_t freq2;
    uint8_t pmf_cfg;
    uint8_t use_dhcp;
} wifi_mgmr_sta_connect_params_t;

typedef struct wifi_mgmr_scan_params {
    uint8_t ssid_length;
    uint8_t ssid_array[MGMR_SSID_LEN];
    uint8_t bssid[6];
    uint8_t bssid_set_flag;
    uint8_t probe_cnt;
    int channels_cnt;
    uint8_t channels[14];
    uint32_t duration;
    bool passive;
} wifi_mgmr_scan_params_t;

typedef struct wifi_mgmr_connect_ind_stat_info {
    uint16_t status_code;
    uint16_t reason_code;
    char ssid[33];
    char passphr[65];
    uint8_t bssid[6];
    uint8_t type_ind;
    uint8_t chan_band;
    uint8_t channel;
    uint8_t security;
} wifi_mgmr_connect_ind_stat_info_t;

typedef void (*scan_item_cb_t)(void *env, void *arg, wifi_mgmr_scan_item_t *item);

int wifi_mgmr_task_start(void);
int wifi_mgmr_sta_connect(const wifi_mgmr_sta_connect_params_t *config);
int wifi_sta_disconnect(void);
int wifi_mgmr_sta_rssi_get(int *rssi);
int wifi_mgmr_sta_connect_ind_stat_get(wifi_mgmr_connect_ind_stat_info_t *info);
int wifi_mgmr_sta_scan(const wifi_mgmr_scan_params_t *config);
int wifi_mgmr_scan_ap_all(void *env, void *arg, scan_item_cb_t cb);
int wifi_mgmr_sta_autoconnect_enable(void);
int wifi_mgmr_sta_autoconnect_disable(void);

/* ---- NetHub and the SDIO message router */
typedef enum {
    NETHUB_CHANNEL_WIFI_STA = 0,
    NETHUB_CHANNEL_WIFI_AP,
    NETHUB_CHANNEL_STACK_STA,
    NETHUB_CHANNEL_STACK_AP,
    NETHUB_CHANNEL_STACK_NAT,
    NETHUB_CHANNEL_BRIDGE,
    NETHUB_CHANNEL_SDIO,
    NETHUB_CHANNEL_MAX
} nethub_channel_t;
enum { NETHUB_OK = 0, NETHUB_ERR_INTERNAL = -7 };
typedef enum {
    NETHUB_WIFI_RX_FILTER_DROP = 0,
    NETHUB_WIFI_RX_FILTER_LOCAL = 1 << 0,
    NETHUB_WIFI_RX_FILTER_HOST = 1 << 1,
    NETHUB_WIFI_RX_FILTER_BOTH = NETHUB_WIFI_RX_FILTER_LOCAL | NETHUB_WIFI_RX_FILTER_HOST,
} nethub_wifi_rx_filter_action_t;
typedef nethub_wifi_rx_filter_action_t (*nethub_wifi_rx_filter_cb_t)(nethub_channel_t src_channel,
                                                                      const struct pbuf *pkt,
                                                                      void *user_ctx);
typedef int (*nethub_vchan_recv_cb_t)(void *cb_arg, uint8_t *data_buff, uint16_t data_size);
int nethub_bootstrap(void);
int nethub_set_wifi_rx_filter(nethub_wifi_rx_filter_cb_t filter_cb, void *user_ctx);
int nethub_vchan_user_send(const void *data, uint16_t len);
int nethub_vchan_user_recv_register(nethub_vchan_recv_cb_t recv_cb, void *cb_arg);
int mr_sdio_drv_lowpower_prepare(void);
int mr_sdio_drv_lowpower_restore(void);

/* ---- log.h: counted by the test, printed with WIFI_TEST_VERBOSE=1 */
void test_log(char level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOG_E(...) test_log('E', __VA_ARGS__)
#define LOG_W(...) test_log('W', __VA_ARGS__)
#define LOG_I(...) test_log('I', __VA_ARGS__)

#endif
