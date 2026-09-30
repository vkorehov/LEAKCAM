/*
 * BLE bring-up follows the SDK's examples/btble/peripheral: rfparam_init() in main, then inside a
 * task btble_controller_init() -> hci_driver_init() -> bt_enable(cb). Differences from the example:
 * no shell on UART0 (UART0 is the K230 link on LEAKCAM), and SMP pairing is gated on USB power.
 */
#include "ble_pairing.h"

#include <stdio.h>
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

#include "evlog.h"
#include "usb_power.h"
#include "wifi_check.h"
#include "wifi_link.h"

/* the last field is 48 bits and BT_UUID_128_ENCODE shifts it by up to 40: it must be a 64-bit literal */
#define LEAKCAM_UUID_BYTES(n) BT_UUID_128_ENCODE(0x4c43a000, 0x4c45, 0x4b43, 0x414d, (n##ULL))
#define LEAKCAM_UUID(n)       BT_UUID_DECLARE_128(LEAKCAM_UUID_BYTES(n))

static struct bt_conn *active_conn;
static char ssid[33];
static char psk[64];
static uint8_t status[3];                /* state, detail LE16: the STATUS characteristic */
static volatile bool checking;

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
    evlog_add(EV_BLE_CREDS, 0, 0);
    set_status(WCHK_SAVED, 0);
}

static void progress(enum wifi_check_state st, uint16_t detail)
{
    set_status(st, detail);
}

static void check_task(void *arg)
{
    static char s[33], k[64];            /* this check's copy: the phone may write again meanwhile */
    uint16_t detail;

    (void)arg;
    memcpy(s, ssid, sizeof(s));
    memcpy(k, psk, sizeof(k));
    enum wifi_check_state st = wifi_check_run(s, strlen(s), k, strlen(k), progress, &detail);
    wifi_link_release(WIFI_OWNER_CHECK);
    if (st != WCHK_INTERNET)
        set_status(st, detail);
    else if (active_conn)
        store(s, k);
    else                                 /* the phone left: it told its user nothing was stored */
        LOG_W("ble: check passed but the phone is gone, not stored\r\n");
    checking = false;
    vTaskDelete(NULL);
}

static ssize_t commit_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    uint8_t cmd = len == 1 ? ((const uint8_t *)buf)[0] : 0;
    if (offset != 0 || (cmd != 0x01 && cmd != 0x02) || ssid[0] == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    if (checking)
        return len;                      /* the running check reports */
    if (cmd == 0x02) {
        store(ssid, psk);
        return len;
    }
    /* the check blocks for up to a minute: its own task, never the BLE host's */
    if (!wifi_link_claim(WIFI_OWNER_CHECK)) {
        set_status(WCHK_BUSY, 0);
        return len;
    }
    checking = true;
    if (xTaskCreate(check_task, "wchk", 1536, NULL, 3, NULL) != pdPASS) {
        checking = false;
        wifi_link_release(WIFI_OWNER_CHECK);
        set_status(WCHK_BUSY, 0);
    }
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
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, ssid_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000003), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, psk_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000004), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, commit_write, NULL),
    /* index 8 is its value: set_status() notifies through it */
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000005), BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, status_read, NULL, NULL),
    BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
};
static struct bt_gatt_service prov_svc = BT_GATT_SERVICE(prov_attrs);

static void set_status(enum wifi_check_state st, uint16_t detail)
{
    status[0] = (uint8_t)st;
    status[1] = (uint8_t)detail;
    status[2] = (uint8_t)(detail >> 8);
    if (active_conn)
        bt_gatt_notify(active_conn, &prov_attrs[8], status, sizeof(status));   /* STATUS value */
}

/* ---------------- pairing policy ---------------- */

static void auth_pairing_confirm(struct bt_conn *conn)
{
    /* no display, no buttons: USB power is the proof of physical access */
    if (usb_present()) {
        bt_conn_auth_pairing_confirm(conn);
    } else {
        LOG_W("ble: pairing refused, not on USB power\r\n");
        bt_conn_auth_cancel(conn);
    }
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
    struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR | BT_LE_AD_GENERAL),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, LEAKCAM_UUID_BYTES(0x000000000001)),
    };
    struct bt_le_adv_param param;
    memset(&param, 0, sizeof(param));
    param.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;     /* 100-150 ms: USB powered, no need to save */
    param.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
    param.options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME;
    int err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
        LOG_E("ble: advertising failed (%d)\r\n", err);
}

static void on_connected(struct bt_conn *conn, u8_t err)
{
    struct bt_conn_info info;
    if (err || bt_conn_get_info(conn, &info) || info.type != BT_CONN_TYPE_LE)
        return;
    active_conn = conn;
    /* ask for encryption right away so the phone's pairing dialog appears on connect */
    bt_conn_set_security(conn, BT_SECURITY_L2);
}

static void on_disconnected(struct bt_conn *conn, u8_t reason)
{
    (void)reason;
    if (conn != active_conn)
        return;
    active_conn = NULL;
    if (usb_present())
        adv_start();                  /* connectable advertising stops on connect; resume it */
}

static struct bt_conn_cb conn_cb = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

static void on_bt_ready(int err)
{
    if (err) {
        LOG_E("ble: bt_enable failed (%d)\r\n", err);
        return;
    }
    bt_addr_le_t addr;
    size_t count = 1;
    bt_id_get(&addr, &count);
    char name[16];
    snprintf(name, sizeof(name), "LEAKCAM-%02X%02X", addr.a.val[1], addr.a.val[0]);
    bt_set_name(name);
    bt_conn_cb_register(&conn_cb);
    bt_conn_auth_cb_register(&auth_cb);
    bt_gatt_service_register(&prov_svc);
    adv_start();
    LOG_I("ble: advertising as %s\r\n", name);
}

void ble_pairing_start(void)
{
    btble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_enable(on_bt_ready);
}

void ble_pairing_stop(void)
{
    bt_le_adv_stop();
    if (active_conn)
        bt_conn_disconnect(active_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
