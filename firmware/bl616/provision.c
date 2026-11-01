#include "provision.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_boot2.h"
#include "easyflash.h"
#include "rfparam_adapter.h"
#include "shell.h"

#include "ble_pairing.h"
#include "presence.h"
#include "usb_power.h"
#include "wifi_link.h"

#define POLL_MS 200

extern int bl_sys_reset_por(void);

/* the partition table, easyflash and the RF calibration once per boot, in this order: rfparam reads its calibration
 * through the partition table, and a second easyflash_init() leaves littlefs unusable (tests/test040). Any failure is
 * a broken flash: fail fast (bflb_mtd_init() would drop the partition table's result) */
static void radio_init(void)
{
    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR || rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();
}

static void provision_task(void *arg)
{
    enum { IDLE, ADVERTISING, STOPPED } state = IDLE;
    (void)arg;
    for (;;) {
        bool usb = usb_present();
        if (usb && state == IDLE) {
            radio_init();
            ble_pairing_start();
            presence_start();
            state = ADVERTISING;
        } else if (!usb && state == ADVERTISING) {
            ble_pairing_stop();             /* a held connection drops at once (test106 step 7) */
            state = STOPPED;
        } else if (usb && state == STOPPED) {
            (void)bl_sys_reset_por();       /* a fresh boot is the proven BLE start; a restart after stop is not.
                                             * Never returns */
        }
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
    wifi_link_credentials_changed();
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

static void cmd_prov(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "erase")) {
        if (ef_del_env("wifi_ssid") != EF_NO_ERR || ef_del_env("wifi_psk") != EF_NO_ERR ||
            ef_del_env("wifi_ap") != EF_NO_ERR) {
            printf("prov: erase failed\r\n");     /* easyflash not up (no USB power since boot) or a flash error */
            return;
        }
        printf("prov: credentials and remembered AP erased\r\n");
        return;
    }
    printf("prov: USB power %s\r\n", usb_present() ? "yes" : "no");
    print_key("wifi_ssid");
    print_key("wifi_psk");
}
SHELL_CMD_EXPORT_ALIAS(cmd_prov, prov, stored Wi-Fi network: length and CRC only [erase]);
