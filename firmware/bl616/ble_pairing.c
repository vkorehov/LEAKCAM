/*
 * BLE bring-up follows the SDK's examples/btble/peripheral: rfparam_init() in main, then inside a
 * task btble_controller_init() -> hci_driver_init() -> bt_enable(cb). Differences from the example:
 * no shell on UART0 (UART0 is the K230 link on LEAKCAM), and SMP pairing is gated on USB power.
 */
#include "ble_pairing.h"

#include <stdlib.h>
#include <string.h>

#include <FreeRTOS.h>
#include "task.h"

#include "bluetooth.h"
#include "conn.h"
#include "gatt.h"
#include "bt_uuid.h"
#include "hci_driver.h"
#include "btble_lib_api.h"
#include "easyflash.h"
#include "log.h"

#include "device_id.h"
#include "wifi_check.h"
#include "wifi_link.h"

/* the last field is 48 bits and BT_UUID_128_ENCODE shifts it by up to 40: it must be a 64-bit literal */
#define LEAKCAM_UUID_BYTES(n) BT_UUID_128_ENCODE(0x4c43a000, 0x4c45, 0x4b43, 0x414d, (n##ULL))
#define LEAKCAM_UUID(n)       BT_UUID_DECLARE_128(LEAKCAM_UUID_BYTES(n))

static struct bt_conn *active_conn;
static volatile bool adv_wanted;         /* ble_pairing_advertise(): provision.c decides when */
static bool adv_on, ready;               /* advertising now; the stack up with a stored PIN */
static volatile bool started;            /* ble_pairing_start() ran: the controller is up */
static uint16_t adv_int_min = BT_GAP_ADV_FAST_INT_MIN_2, adv_int_max = BT_GAP_ADV_FAST_INT_MAX_2;   /* 0.625 ms */
static char ssid[33];
static char psk[64];
static uint8_t status[7];                /* state, detail LE16, attempt LE32: the STATUS characteristic */
static uint32_t attempt;                 /* the phone's id of the last commit: STATUS answers for it */
static volatile bool checking;

/* the controller's init resets the PDS block (SDK weak hook, GLB_AHB_MCU_SW_PDS); the BL616's USB PHY is controlled
 * from PDS: with the SDK's reset the USB device dropped at BLE start and never came back (A/B, tests/test040,
 * 2026-10-29). The setup firmware's PDS sleep runs without it too (tests/test120); see ble_pairing_sleep_ok(). */
void btblecontroller_software_pds_reset(void)
{
}

/* ---------------- GATT ---------------- */

static ssize_t write_str(const void *buf, u16_t len, u16_t offset, char *dst, size_t cap)
{
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len >= cap)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    memcpy(dst, buf, len);
    dst[len] = 0;
    return len;
}

static ssize_t ssid_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    return write_str(buf, len, offset, ssid, sizeof(ssid));
}

static ssize_t psk_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    if (offset == 0 && len > 0 && len < 8)          /* WPA2 passphrase is 8..63 characters */
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    return write_str(buf, len, offset, psk, sizeof(psk));
}

static void set_status(enum wifi_check_state st, uint16_t detail);

/* stored on the BL616: it is the Wi-Fi device, and the K230 is usually powered off */
static void store(const char *s, const char *k)
{
    if (ef_set_env_blob("wifi_ssid", s, strlen(s)) != EF_NO_ERR ||
        ef_set_env_blob("wifi_psk", k, strlen(k)) != EF_NO_ERR) {
        set_status(WCHK_SAVE_FAILED, 0);
        return;
    }
    LOG_I("ble: Wi-Fi credentials stored for \"%s\"\r\n", s);
    set_status(WCHK_SAVED, 0);
    ble_credentials_stored();
}

static void progress(enum wifi_check_state st, uint16_t detail)
{
    set_status(st, detail);
}

static volatile bool warming;            /* warm_task bringing Wi-Fi up */

static void warm_task(void *arg)
{
    (void)arg;
    wifi_link_warm();
    if (!active_conn && !checking)
        wifi_link_cool();                /* the phone left meanwhile */
    warming = false;
    vTaskDelete(NULL);
}

static void check_task(void *arg)
{
    static char s[33], k[64];            /* this check's copy: the phone may write again meanwhile */
    uint16_t detail;

    (void)arg;
    while (warming)                      /* one Wi-Fi start at a time */
        vTaskDelay(pdMS_TO_TICKS(10));
    memcpy(s, ssid, sizeof(s));
    memcpy(k, psk, sizeof(k));
    enum wifi_check_state st = wifi_check_run(s, strlen(s), k, strlen(k), &wifi_check_probe, progress, &detail);
    if (st != WCHK_INTERNET)
        set_status(st, detail);
    else                                 /* also with the phone gone: it reconnects and reads STATUS (tests/test110) */
        store(s, k);
    checking = false;
    vTaskDelete(NULL);
}

static ssize_t commit_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    const uint8_t *b = buf;
    uint8_t cmd = len == 1 || len == 5 ? b[0] : 0;
    if (offset != 0 || (cmd != 0x01 && cmd != 0x02) || ssid[0] == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    if (checking)
        return len;                      /* the running check reports */
    attempt = len == 5 ? b[1] | b[2] << 8 | b[3] << 16 | (uint32_t)b[4] << 24 : 0;
    status[0] = WCHK_IDLE;               /* a new commit: its first state is always notified, also with the same id */
    if (cmd == 0x02) {
        store(ssid, psk);
        return len;
    }
    /* the check blocks for up to a minute: its own task, never the BLE host's (wifi_check_run claims the radio) */
    checking = true;
    set_status(WCHK_JOINING, 0);         /* a STATUS read after this write's answer is this check's */
    if (xTaskCreate(check_task, "wchk", 1536, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
    return len;
}

static ssize_t status_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, u16_t len, u16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, status, sizeof(status));
}

static void status_ccc_changed(const struct bt_gatt_attr *attr, u16_t value)
{
    (void)attr; (void)value;
}

static struct bt_gatt_attr prov_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(LEAKCAM_UUID(0x000000000001)),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000002), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, ssid_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000003), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, psk_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000004), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, commit_write, NULL),
    /* index 8 is its value: set_status() notifies through it */
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000005), BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_AUTHEN, status_read, NULL, NULL),
    BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_AUTHEN),
};
static struct bt_gatt_service prov_svc = BT_GATT_SERVICE(prov_attrs);

static void set_status(enum wifi_check_state st, uint16_t detail)
{
    uint8_t next[sizeof(status)] = { (uint8_t)st, (uint8_t)detail, (uint8_t)(detail >> 8) };
    for (int i = 0; i < 4; i++)
        next[3 + i] = (uint8_t)(attempt >> (8 * i));
    if (!memcmp(next, status, sizeof(status)))
        return;                          /* the same state again (JOINING from the commit, then from the check) */
    memcpy(status, next, sizeof(status));
    /* not subscribed or no buffer: the app reads STATUS too, the notification only saves it the wait */
    if (active_conn)
        (void)bt_gatt_notify(active_conn, &prov_attrs[8], status, sizeof(status));   /* STATUS value */
}

/* ---------------- pairing policy ---------------- */

/* the pairing PIN (LE Secure Connections passkey entry, user 2026-11-01: only the app pairs): 6 digits stored in
 * easyflash ("pair_pin") once at manufacture (`prov pin`, the console inside the enclosure). How it is derived is
 * known only to the server, which gives it to the app right before it pairs (GET /v1/devices/<id>/pin): neither the
 * firmware nor the app holds the derivation. 0..999999, or -1 when none is stored (the board cannot be set up then). */
int ble_pairing_pin(void)
{
    char v[7];
    size_t saved = 0;
    size_t n = ef_get_env_blob("pair_pin", v, sizeof(v) - 1, &saved);
    if (saved != 6 || n != 6)
        return -1;
    v[6] = 0;
    for (int i = 0; i < 6; i++)
        if (v[i] < '0' || v[i] > '9')
            return -1;
    return atoi(v);
}

/* Just Works (a phone that enters no PIN): refused, the provisioning service needs an authenticated link */
static void auth_pairing_confirm(struct bt_conn *conn)
{
    LOG_W("ble: pairing without the PIN refused\r\n");
    (void)bt_conn_auth_cancel(conn);        /* refused either way: the link stays unauthenticated */
}

/* its presence makes the board's IO capability "display only": the phone enters the passkey. Never printed */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    (void)conn;
    (void)passkey;
}

static void auth_cancel(struct bt_conn *conn)
{
    (void)conn;
    LOG_W("ble: pairing cancelled\r\n");
}

static void auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
    (void)conn;
    (void)bonded;
    LOG_I("ble: paired, bonded=%d\r\n", bonded);
}

static void auth_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    (void)conn;
    (void)reason;
    LOG_W("ble: pairing failed, reason %d\r\n", (int)reason);
}

static const struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
    .pairing_confirm = auth_pairing_confirm,
    .pairing_complete = auth_pairing_complete,
    .pairing_failed = auth_pairing_failed,
};

/* ---------------- connection / advertising ---------------- */

static void adv_start(void)
{
    /* local, as in the SDK example: BT_DATA_BYTES builds compound literals, and bt_le_adv_start()
     * copies the payload into the controller before returning */
    /* manufacturer data, company 0xFFFF (none): the device ID (device_id.c) and a flags byte (bit 0: a Wi-Fi network
     * is stored). The app tells a LEAKCAM it set up before from a new one and shows a stored network as hidden,
     * without connecting. In the scan response with the name (11 + 14 bytes): the advertisement is full (21 + 10) */
    uint8_t mfg[9] = { 0xFF, 0xFF };
    size_t saved = 0;
    device_id_bytes(&mfg[2]);
    (void)ef_get_env_blob("wifi_ssid", NULL, 0, &saved);   /* only the length: 0 when none is stored */
    mfg[8] = saved ? 0x01 : 0x00;
    struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR | BT_LE_AD_GENERAL),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, LEAKCAM_UUID_BYTES(0x000000000001)),
    };
    struct bt_data sd[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg, sizeof(mfg)),
    };
    struct bt_le_adv_param param;
    memset(&param, 0, sizeof(param));
    param.interval_min = adv_int_min;                   /* 100-150 ms unless set (ble_pairing_adv_interval) */
    param.interval_max = adv_int_max;
    param.options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME;
    if (bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd)))
        vAssertCalled();                 /* not advertising: the board could never be set up */
    adv_on = true;
}

static void on_connected(struct bt_conn *conn, u8_t err)
{
    struct bt_conn_info info;
    if (err || bt_conn_get_info(conn, &info) || info.type != BT_CONN_TYPE_LE)
        return;
    /* no USB power gate since 2026-11-01 (user): the pairing PIN proves the phone; on battery too */
    adv_on = false;                      /* connectable advertising stops on connect */
    active_conn = conn;
    /* each connection starts empty: an open network is "no passphrase written" - the SDK's ATT layer refuses
     * zero-length writes (BFLB_HOST_PARAMETER_CHECK, 0x13) - and an earlier session's values are never reused */
    memset(ssid, 0, sizeof(ssid));
    memset(psk, 0, sizeof(psk));
    /* ask for encryption right away so the phone's pairing dialog appears on connect */
    if (bt_conn_set_security(conn, BT_SECURITY_L3))   /* L3: authenticated (passkey), not Just Works */
        LOG_W("ble: security request failed\r\n");   /* the phone pairs at its first encrypted write instead */
    /* Wi-Fi up while the phone pairs and writes: the check then starts at once (tests/test106) */
    if (!warming) {
        warming = true;
        if (xTaskCreate(warm_task, "wwarm", 1536, NULL, 3, NULL) != pdPASS)
            vAssertCalled();
    }
}

static void on_disconnected(struct bt_conn *conn, u8_t reason)
{
    (void)reason;
    if (conn != active_conn)
        return;
    active_conn = NULL;
    if (!checking && !warming)
        wifi_link_cool();
    if (adv_wanted)
        adv_start();                  /* connectable advertising stopped on connect; resume it */
}

static struct bt_conn_cb conn_cb = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* breadcrumbs (wifi_link.c, kept across a reset in the test firmware): where a BLE start hung, 0x70-0x75 */
void wl_trace(uint8_t code);

static void on_bt_ready(int err)
{
    wl_trace(0x74);
    if (err)
        vAssertCalled();                 /* the BLE host did not come up */
    const char *name = device_id();      /* the serial everywhere (USB, BLE, server), no prefix (user 2026-11-01) */
    int pin = ble_pairing_pin();
    if (pin < 0) {
        LOG_E("ble: no pairing PIN stored (prov pin): not advertising, the board cannot be set up\r\n");
        return;
    }
    if (bt_set_name(name) || bt_passkey_set((unsigned int)pin))
        vAssertCalled();
    bt_conn_cb_register(&conn_cb);
    if (bt_conn_auth_cb_register(&auth_cb) || bt_gatt_service_register(&prov_svc))
        vAssertCalled();
    ready = true;
    if (adv_wanted)
        adv_start();
    wl_trace(0x75);
    LOG_I("ble: ready as %s\r\n", name);
}

void ble_pairing_start(void)
{
    started = true;
    wl_trace(0x70);
    btble_controller_init(configMAX_PRIORITIES - 1);
    wl_trace(0x71);
    if (hci_driver_init())
        vAssertCalled();
    wl_trace(0x72);
    if (bt_enable(on_bt_ready))
        vAssertCalled();
    wl_trace(0x73);
}

bool ble_pairing_connected(void)
{
    return active_conn != NULL;
}

/* The low-power sleep path with the controller up but idle (no advertising, no connection: no wake deadline) wedged the
 * BLE core: the SDK asks the controller for its deadline (btble_controller_sleep) before any veto, the core went to
 * sleep for good, and after any software reset its init spun forever; only the EN pin cleared it (tests/test120,
 * 2026-11-01). The setup firmware's idle hook does not enter that path while this is false. */
bool ble_pairing_sleep_ok(void)
{
    return !started || adv_on || active_conn != NULL;
}

/* test114 (the test firmware's `pt_ble adv`): the advertising interval, restarted at once when advertising */
void ble_pairing_adv_interval(unsigned int ms)
{
    unsigned int units = ms * 1000u / 625u;
    if (units < 32u || units > 16384u)
        vAssertCalled();                 /* outside BLE's 20 ms .. 10.24 s: the caller's bug */
    adv_int_min = adv_int_max = (uint16_t)units;
    if (adv_on && !active_conn) {
        if (bt_le_adv_stop())
            vAssertCalled();
        adv_on = false;
        adv_start();
    }
}

/* only advertising starts and stops: the stack stays up (a BLE restart after a stop was never proven, test106) */
void ble_pairing_advertise(bool on)
{
    adv_wanted = on;
    if (!ready || active_conn)
        return;                          /* on_bt_ready / on_disconnected follow adv_wanted */
    if (on && !adv_on)
        adv_start();
    else if (!on && adv_on) {
        if (bt_le_adv_stop())
            vAssertCalled();
        adv_on = false;
    }
}
