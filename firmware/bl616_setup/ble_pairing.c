/*
 * BLE bring-up follows the SDK's examples/btble/peripheral: rfparam_init() in main, then inside a
 * task btble_controller_init() -> hci_driver_init() -> bt_enable(cb). Differences from the example:
 * no shell on UART0 (UART0 is the K230 link on LEAKCAM), and SMP pairing is gated on USB power.
 */
#include "ble_pairing.h"
#include "battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FreeRTOS.h>
#include "task.h"

#include "bluetooth.h"
#include "conn.h"
#include "conn_internal.h"   /* conn->state */
#include "hci_core.h"       /* bt_dev.flags */
#include "gatt.h"
#include "bt_uuid.h"
#include "hci_driver.h"
#include "btble_lib_api.h"
#include "easyflash.h"
#include "log.h"

#include "device_id.h"
#include "api.h"
#include "wifi_check.h"
#include "wifi_link.h"
#include "provision.h"

/* the last field is 48 bits and BT_UUID_128_ENCODE shifts it by up to 40: it must be a 64-bit literal */
#define LEAKCAM_UUID_BYTES(n) BT_UUID_128_ENCODE(0x4c43a000, 0x4c45, 0x4b43, 0x414d, (n##ULL))
#define LEAKCAM_UUID(n)       BT_UUID_DECLARE_128(LEAKCAM_UUID_BYTES(n))

static struct bt_conn *active_conn;
static volatile bool adv_wanted;         /* ble_pairing_advertise(): provision.c decides when */
static void (*link_cb)(bool up);
static bool adv_on, ready;               /* advertising now; the stack up with a stored PIN */
static uint16_t adv_int_min = BT_GAP_ADV_FAST_INT_MIN_2, adv_int_max = BT_GAP_ADV_FAST_INT_MAX_2;   /* 0.625 ms */

/* the controller's init resets the PDS block (SDK weak hook, GLB_AHB_MCU_SW_PDS); the BL616's USB PHY is controlled
 * from PDS: with the SDK's reset the USB device dropped at BLE start and never came back (A/B, tests/test040,
 * 2026-10-29). The setup firmware does that reset first in main(), before USB is up (bugs/bug-006); its PDS sleep runs
 * without one here (tests/test120); see ble_pairing_sleep_ok(). */
void btblecontroller_software_pds_reset(void)
{
}

/* ---------------- GATT: the provisioning (provision.c) as characteristics ---------------- */

static ssize_t ssid_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    return provision_ssid(buf, len) ? BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN) : len;
}

static ssize_t psk_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    return provision_psk(buf, len) ? BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN) : len;
}

static volatile bool warming;            /* warm_task bringing Wi-Fi up */

static void warm_task(void *arg)
{
    (void)arg;
    wifi_link_warm();
    if (!active_conn && !provision_checking())
        wifi_link_cool();                /* the phone left meanwhile */
    warming = false;
    vTaskDelete(NULL);
}

/* one Wi-Fi start at a time: a check waits in its own task for the warm-up the connection started */
void provision_before_check(void)
{
    while (warming)
        vTaskDelay(pdMS_TO_TICKS(10));
}

static ssize_t commit_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    (void)conn; (void)attr; (void)flags;
    const uint8_t *b = buf;
    if (offset != 0 || (len != 1 && len != 5))
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    uint32_t id = len == 5 ? b[1] | b[2] << 8 | b[3] << 16 | (uint32_t)b[4] << 24 : 0;
    return provision_commit(b[0], id) ? BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN) : len;
}

static ssize_t status_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, u16_t len, u16_t offset)
{
    uint8_t st[PROVISION_STATUS_LEN];
    provision_status(st);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, st, sizeof(st));
}

static void status_ccc_changed(const struct bt_gatt_attr *attr, u16_t value)
{
    (void)attr; (void)value;
}

static struct bt_gatt_attr provision_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(LEAKCAM_UUID(0x000000000001)),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000002), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, ssid_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000003), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, psk_write, NULL),
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000004), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_AUTHEN, NULL, commit_write, NULL),
    /* index 8 is its value: status_notify() notifies through it */
    BT_GATT_CHARACTERISTIC(LEAKCAM_UUID(0x000000000005), BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_AUTHEN, status_read, NULL, NULL),
    BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_AUTHEN),
};
static struct bt_gatt_service provision_svc = BT_GATT_SERVICE(provision_attrs);

/* not subscribed or no buffer: the app reads STATUS too, the notification only saves it the wait */
static void status_notify(const uint8_t st[PROVISION_STATUS_LEN])
{
    if (active_conn)
        (void)bt_gatt_notify(active_conn, &provision_attrs[8], st, PROVISION_STATUS_LEN);   /* STATUS value */
}

/* ---------------- pairing policy ---------------- */

/* A phone that does not pair with the PIN is dropped: at once when its pairing fails or is refused, and 10 s after it
 * connected if it has not paired by then (the app pairs in 1-3 s). Any phone in range can open a link to a connectable
 * board; without this it could hold the one connection and keep the owner's app out (user 2026-11-04: no neighbour
 * holds the device) */
#define AUTH_DEADLINE_MS 10000

static struct bt_conn *active_conn;
static TaskHandle_t auth_task;
static volatile TickType_t auth_until;
static volatile bool auth_armed;

/* the deadline set (or moved to now: drop at once) or cleared; the drop itself in auth_task_fn: the host refuses an HCI
 * command from the timer task (assert, 2026-11-04) and the SMP callbacks run in the host's own task */
static void auth_arm(bool on, TickType_t in)
{
    auth_until = xTaskGetTickCount() + in;
    auth_armed = on;
    xTaskNotifyGive(auth_task);
}

static void auth_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (auth_armed) {
            int32_t left = (int32_t)(auth_until - xTaskGetTickCount());
            wait = left > 0 ? (TickType_t)left : 0;
        }
        if (wait && ulTaskNotifyTake(pdTRUE, wait))
            continue;                         /* armed, cleared or moved: look again */
        if (auth_armed && (int32_t)(xTaskGetTickCount() - auth_until) >= 0) {
            auth_armed = false;
            struct bt_conn *conn = active_conn;
            if (conn && bt_conn_get_security(conn) < BT_SECURITY_L3) {
                LOG_W("ble: phone dropped: not paired with the PIN\r\n");
                (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
            }
        }
    }
}

/* the pairing PIN (LE Secure Connections passkey entry, user 2026-11-01: only the app pairs): 6 digits stored in
 * easyflash ("pair_pin") once at manufacture (`provision pin`, the console inside the enclosure). How it is derived is
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
    if (conn == active_conn)
        auth_arm(false, 0);
    LOG_I("ble: paired, bonded=%d\r\n", bonded);
}

static void auth_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_W("ble: pairing failed, reason %d\r\n", (int)reason);
    if (conn == active_conn)
        auth_arm(true, 0);                    /* dropped at once */
}

static const struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
    .pairing_confirm = auth_pairing_confirm,
    .pairing_complete = auth_pairing_complete,
    .pairing_failed = auth_pairing_failed,
};

/* ---------------- connection / advertising ---------------- */

/* USB power for the advertisement's flags byte (bit 1: the app shows a plug, else a battery; DESIGN.md 5.3.4): the
 * setup firmware's setup_sleep.c reads PGOOD; the test firmware (no PGOOD reader) says none. Read when advertising
 * starts: the unplug restarts it (ble_pairing_adv_interval), a plug-in switches to the full firmware */
__attribute__((weak)) bool ble_pairing_usb_power(void)
{
    return false;
}

static void adv_start(void)
{
    /* local, as in the SDK example: BT_DATA_BYTES builds compound literals, and bt_le_adv_start()
     * copies the payload into the controller before returning */
    /* manufacturer data, company 0xFFFF (none): the device ID (device_id.c), a flags byte (bit 0: a Wi-Fi network
     * is stored, bit 1: USB power) and the battery in 20 mV steps (the last reading on battery, 0xFF none; DESIGN.md
     * 5.3.9). The app tells a LEAKCAM it set up before from a new one, shows a stored network as hidden and the battery,
     * without connecting. In the scan response with the name (12 + 14 of 31 bytes): the advertisement is full (21 + 10) */
    uint8_t mfg[10] = { 0xFF, 0xFF };
    int mv = battery_mv();
    size_t saved = 0;
    device_id_bytes(&mfg[2]);
    (void)ef_get_env_blob("wifi_ssid", NULL, 0, &saved);   /* only the length: 0 when none is stored */
    mfg[8] = (saved ? 0x01 : 0x00) | (ble_pairing_usb_power() ? 0x02 : 0x00);
    mfg[9] = mv < 0 ? 0xFF : (uint8_t)(mv / 20 > 254 ? 254 : mv / 20);
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
    auth_arm(true, pdMS_TO_TICKS(AUTH_DEADLINE_MS));
    if (link_cb)
        link_cb(true);
    /* each connection starts empty: an open network is "no passphrase written" - the SDK's ATT layer refuses
     * zero-length writes (BFLB_HOST_PARAMETER_CHECK, 0x13) - and an earlier session's values are never reused */
    provision_clear();
    /* ask for encryption right away: Android's own pairing request (the app's createBond) carries no MITM flag, and
     * the board refuses pairing without the PIN; this request makes it a passkey pairing. With the app's createBond
     * pending the app enters the PIN; without it Samsung shows its "Pair" dialog first (tests/test106, 2026-11-02) */
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
    LOG_I("ble: disconnected, reason 0x%02x%s\r\n", reason, conn == active_conn ? "" : " (not the session)");
    if (conn != active_conn)
        return;
    active_conn = NULL;
    auth_arm(false, 0);
    if (!provision_checking() && !warming)
        wifi_link_cool();
    if (adv_wanted)
        adv_start();                  /* connectable advertising stopped on connect; resume it */
    if (link_cb)
        link_cb(false);               /* after the restart: see ble_pairing_link_cb() */
}

void ble_pairing_link_cb(void (*cb)(bool up))
{
    link_cb = cb;
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
        LOG_E("ble: no pairing PIN stored (provision pin): not advertising, the board cannot be set up\r\n");
        return;
    }
    if (bt_set_name(name) || bt_passkey_set((unsigned int)pin))
        vAssertCalled();
    if (xTaskCreate(auth_task_fn, "ble_auth", 512, NULL, 4, &auth_task) != pdPASS)
        vAssertCalled();
    provision_on_status(status_notify);
    bt_conn_cb_register(&conn_cb);
    if (bt_conn_auth_cb_register(&auth_cb) || bt_gatt_service_register(&provision_svc))
        vAssertCalled();
    ready = true;
    if (adv_wanted)
        adv_start();
    wl_trace(0x75);
    LOG_I("ble: ready as %s\r\n", name);
}

void ble_pairing_start(void)
{
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
 * 2026-11-01). The setup firmware's idle hook does not enter that path while this is false. Nor before BLE runs: a PDS
 * sleep before the controller's start (sleep begun while ble_start_task was still in app_clock_init) wedged it the
 * same way (bugs/bug-006, 2026-11-03: a setup firmware started on battery never advertised). The host's own state, not
 * this module's flags: a stale flag would say "advertising" while BLE is idle (bug-006 follow-up). A connection keeps
 * the chip awake anyway (ble_pairing_link_cb). A board that never starts BLE (no PIN, ble_pairing_never()) sleeps: the
 * SDK only asks a controller that runs (rw_main_task_hdl). */
static volatile bool never;

void ble_pairing_never(void)
{
    never = true;
}

bool ble_pairing_sleep_ok(void)
{
    return never || atomic_test_bit(bt_dev.flags, BT_DEV_ADVERTISING);
}

static void link_print(struct bt_conn *conn, void *data)
{
    (void)data;
    printf("ble: link %p state %d%s\r\n", (void *)conn, (int)conn->state, conn == active_conn ? " (the session)" : "");
}

/* the test bed's lp_sleepstat: what the veto reads (bugs/bug-006 cases), dropped from the product by the linker */
void ble_pairing_print_state(void)
{
    printf("ble: never %d, host advertising %d, module adv_on %d, session %s\r\n", (int)never,
           (int)atomic_test_bit(bt_dev.flags, BT_DEV_ADVERTISING), (int)adv_on, active_conn ? "yes" : "no");
    bt_conn_foreach(BT_CONN_TYPE_LE, link_print, NULL);
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
