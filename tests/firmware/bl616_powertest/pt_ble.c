/*
 * test040: the product's BLE provisioning (firmware/bl616/ble_pairing.c + wifi_check.c) on the test firmware. The
 * phone side is the instrumented test of android/ (BleOp), driven over adb by tests/test040's run.py.
 *
 *   pt_ble on         RF parameters, then BLE as the product starts it: advertising as LEAKCAM-xxyy, pairing with
 *                     the stored PIN (passkey entry); unlike the product (advertising windows on battery while no
 *                     network is stored, provision.c) it advertises all the time once started.
 *   pt_ble            started?, USB power, the stored credentials (as pt_ble creds)
 *   pt_ble creds      wifi_ssid / wifi_psk in easyflash: length and CRC-32 each, never the passphrase itself
 *   pt_ble erase      wifi_ssid, wifi_psk and the remembered AP (wifi_ap) deleted
 *   pt_ble pin <6>    the BLE pairing PIN stored (as the product's `prov pin`; provision_pin.py from make flash)
 *   pt_ble adv <ms>   the advertising interval (20 .. 10240 ms; test114's sweep)
 *   pt_ble decoy      a fixed made-up network (t110-decoy / password1) stored directly: test110 then tells "stored"
 *                     from "not stored" without a 16 s save through the app
 */
#include "tests.h"

#if PT_TEST_BLE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "easyflash.h"
#include "shell.h"

#include "ble_pairing.h"

void wl_trace(uint8_t code);   /* pt_wifi.c: breadcrumbs kept across a reset (pt_trace) */

bool usb_present(void);                    /* main.c */

static bool started;

static uint32_t crc32(const uint8_t *p, size_t n)  /* zlib's CRC-32: the runner compares with ~/.leakcam_wifi */
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
        printf("pt_ble: %s none\r\n", key);
    else
        printf("pt_ble: %s len %u crc %08lx\r\n", key, (unsigned)saved, (unsigned long)crc32(v, n));
}

static void ble_task(void *arg)
{
    (void)arg;
    ble_pairing_start();
    ble_pairing_advertise(true);            /* the test firmware advertises whenever BLE is on */
    vTaskDelete(NULL);
}

static void cmd_ble(int argc, char **argv)
{
    const char *op = argc > 1 ? argv[1] : "";
    if (pt_ef_once() != 0) {
        printf("pt_ble: easyflash init failed\r\n");
        return;
    }
    if (!strcmp(op, "on")) {
        if (started) {
            printf("pt_ble: already on\r\n");
            return;
        }
        wl_trace(0x7e);
        if (!pt_radio_init())
            return;
        wl_trace(0x7f);
        started = true;
        if (xTaskCreate(ble_task, "ble", 1024, NULL, 3, NULL) != pdPASS)
            vAssertCalled();
        printf("pt_ble: starting (USB power %s)\r\n", usb_present() ? "yes" : "no");
    } else if (!strcmp(op, "erase")) {
        if (ef_del_env("wifi_ssid") != EF_NO_ERR || ef_del_env("wifi_psk") != EF_NO_ERR ||
            ef_del_env("wifi_ap") != EF_NO_ERR)
            printf("pt_ble: erase failed\r\n");
        else
            printf("pt_ble: credentials and remembered AP erased\r\n");
    } else if (!strcmp(op, "adv") && argc > 2) {
        int ms = atoi(argv[2]);
        if (ms < 20 || ms > 10240)
            printf("pt_ble: adv: 20 .. 10240 ms\r\n");
        else {
            ble_pairing_adv_interval((unsigned int)ms);
            printf("pt_ble: advertising every %d ms\r\n", ms);
        }
    } else if (!strcmp(op, "pin") && argc > 2) {
        if (strlen(argv[2]) != 6 || strspn(argv[2], "0123456789") != 6)
            printf("pt_ble: pin: 6 digits\r\n");
        else if (ef_set_env_blob("pair_pin", argv[2], 6) != EF_NO_ERR)
            printf("pt_ble: pin store failed\r\n");
        else
            printf("pt_ble: pairing PIN stored (it takes effect at the next boot)\r\n");
    } else if (!strcmp(op, "decoy")) {
        if (ef_set_env_blob("wifi_ssid", "t110-decoy", 10) != EF_NO_ERR ||
            ef_set_env_blob("wifi_psk", "password1", 9) != EF_NO_ERR)
            printf("pt_ble: decoy store failed\r\n");
        else
            printf("pt_ble: decoy stored\r\n");
    } else if (!strcmp(op, "creds") || !*op) {
        if (!*op)
            printf("pt_ble: %s, USB power %s\r\n", started ? "on" : "off", usb_present() ? "yes" : "no");
        print_key("wifi_ssid");
        print_key("wifi_psk");
        printf("pt_ble: pairing PIN %s\r\n", ble_pairing_pin() < 0 ? "none (pt_ble pin)" : "stored");
    } else
        printf("usage: pt_ble [on | creds | erase | decoy | pin <6 digits> | adv <ms>]\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_ble, pt_ble, test040 BLE provisioning: on | creds | erase | decoy | pin <6 digits> | adv <ms>);
#endif
