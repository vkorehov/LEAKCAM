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

#include "usb_power.h"

/* the last field is 48 bits and BT_UUID_128_ENCODE shifts it by up to 40: it must be a 64-bit literal */
#define LEAKCAM_UUID_BYTES(n) BT_UUID_128_ENCODE(0x4c43a000, 0x4c45, 0x4b43, 0x414d, (n##ULL))
#define LEAKCAM_UUID(n)       BT_UUID_DECLARE_128(LEAKCAM_UUID_BYTES(n))

static struct bt_conn *active_conn;
static char ssid[33];
static char psk[64];

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

static ssize_t commit_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    if (offset != 0 || len != 1 || ((const uint8_t *)buf)[0] != 0x01)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    if (ssid[0] == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    /* stored on the BL616: it is the Wi-Fi device, and the K230 is usually powered off */
    if (ef_set_env_blob("wifi_ssid", ssid, strlen(ssid)) != EF_NO_ERR ||
        ef_set_env_blob("wifi_psk", psk, strlen(psk)) != EF_NO_ERR)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    LOG_I("ble: Wi-Fi credentials stored for \"%s\"\r\n", ssid);
    return len;
}

static struct bt_gatt_attr prov_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(LEAKCAM_UUID(0x000000000001)),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000002), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, ssid_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000003), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, psk_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000004), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, commit_write, NULL),
};
static struct bt_gatt_service prov_svc = BT_GATT_SERVICE(prov_attrs);

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

bool ble_pairing_connected(void)
{
    return active_conn != NULL;
}
