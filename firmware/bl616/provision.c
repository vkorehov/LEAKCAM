#include "provision.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_boot2.h"
#include "bflb_mtimer.h"
#include "easyflash.h"
#include "rfparam_adapter.h"
#include "shell.h"

#include "ble_pairing.h"
#include "presence.h"
#include "usb_power.h"
#include "wifi_link.h"

#define POLL_MS   200
#define WINDOW_S  60                     /* on battery, no network stored or presence lost: advertise this long ... */
#define PERIOD_S  300                    /* ... every 5 minutes (user 2026-11-01: a new board found without a cable) */
#define WAKE_S    120                    /* woken by a phone through the server: time to find it and tap Change */

static volatile bool configured;         /* a Wi-Fi network is stored */
static volatile bool as_battery;         /* `prov battery on`: the battery rule on USB power too (tests, not stored) */
static const char *volatile adv_why = "not yet";   /* the battery rule now, for `prov` (tests) */

/* the partition table, easyflash and the RF calibration once per boot, in this order: rfparam reads its calibration
 * through the partition table, and a second easyflash_init() leaves littlefs unusable (tests/test040). Any failure is
 * a broken flash: fail fast (bflb_mtd_init() would drop the partition table's result) */
static void radio_init(void)
{
    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR || rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();
}

/* BLE from boot, on battery too (the pairing PIN proves the phone, user 2026-11-01): advertising always on USB power
 * (a network can be changed); on battery while no network is stored or presence is lost (the last check-in did not
 * reach the server, for whatever reason: a phone nearby sets the Wi-Fi at once, user 2026-11-01), 60 s every
 * 5 minutes, the first window at once when the need starts (boot, an erase, the loss); and for 2 minutes when a phone
 * out of range asked the server to wake it (the check-in answer's "ble":1, tests/test119) */
static void provision_task(void *arg)
{
    (void)arg;
    radio_init();
    configured = wifi_link_has_credentials();
    presence_start();                       /* always: checks in whenever a network is stored (use case 2) */
    ble_pairing_start();
    uint32_t since = 0;                     /* when the need began, in ms of this loop */
    bool needed = false;
    for (uint32_t ms = 0;; ms += POLL_MS) {
        bool lost = configured && presence_lost();
        bool need = !configured || lost;
        if (need && !needed)
            since = ms;
        needed = need;
        uint32_t w = presence_woken_ms();
        bool woken = w && (uint32_t)bflb_mtimer_get_time_ms() - w < WAKE_S * 1000u;
        bool window = (need && ((ms - since) / 1000) % PERIOD_S < WINDOW_S) || woken;
        adv_why = woken ? "on (woken by a phone)" : !need ? "off (configured, present)" :
                  !window ? "off (between windows)" : !configured ? "on (no network stored)" : "on (presence lost)";
        ble_pairing_advertise((usb_present() && !as_battery) || window);
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void provision_start(void)
{
    if (xTaskCreate(provision_task, "prov", 1536, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
}

/* new credentials: the remembered AP of the old network no longer applies */
void ble_credentials_stored(void)
{
    configured = true;
    wifi_link_credentials_changed();
    presence_now();                         /* present again (or not) at once, not at the next interval */
}

/* zlib's CRC-32, as the test runners compute it over ~/.leakcam_wifi: the stored network is checked without ever
 * printing the passphrase */
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static void print_key(const char *key)
{
    uint8_t v[64];
    size_t saved = 0;
    size_t n = ef_get_env_blob(key, v, sizeof(v), &saved);
    if (saved == 0)
        printf("prov: %s none\r\n", key);
    else
        printf("prov: %s len %u crc %08lx\r\n", key, (unsigned)saved, (unsigned long)crc32(v, n));
}

/* the BLE pairing PIN, written once when the firmware is flashed (tools/power-bringup/provision_pin.py from make
 * flash); never printed back */
static void store_pin(const char *pin)
{
    if (strlen(pin) != 6 || strspn(pin, "0123456789") != 6) {
        printf("prov: pin: 6 digits\r\n");
        return;
    }
    if (ef_set_env_blob("pair_pin", pin, 6) != EF_NO_ERR) {
        printf("prov: pin store failed\r\n");
        return;
    }
    printf("prov: pairing PIN stored (it takes effect at the next boot)\r\n");
}

static void cmd_prov(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "pin")) {
        store_pin(argv[2]);
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "battery")) {
        /* the tests see the battery rule on the radio without unplugging USB-C 1 (tests/test117, test119) */
        as_battery = !strcmp(argv[2], "on");
        printf("prov: battery rule on USB power %s\r\n", as_battery ? "on" : "off");
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "erase")) {
        if (ef_del_env("wifi_ssid") != EF_NO_ERR || ef_del_env("wifi_psk") != EF_NO_ERR ||
            ef_del_env("wifi_ap") != EF_NO_ERR) {
            printf("prov: erase failed\r\n");     /* easyflash not up yet or a flash error */
            return;
        }
        configured = false;
        printf("prov: credentials and remembered AP erased\r\n");
        return;
    }
    printf("prov: USB power %s\r\n", usb_present() ? "yes" : "no");
    printf("prov: advertising %s, on battery %s\r\n", usb_present() && !as_battery ? "yes (USB power)" : "as on battery",
           adv_why);
    printf("prov: pairing PIN %s\r\n", ble_pairing_pin() < 0 ? "none (prov pin)" : "stored");
    print_key("wifi_ssid");
    print_key("wifi_psk");
}
SHELL_CMD_EXPORT_ALIAS(cmd_prov, prov, stored Wi-Fi network: length and CRC only [erase | pin <6 digits> | battery on|off]);
