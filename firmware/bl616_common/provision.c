/*
 * `provision` in both firmwares: the stored Wi-Fi network (length and CRC only), its erase, and the BLE pairing PIN written
 * once when the firmware is flashed (tools/power-bringup/provision_pin.py from make flash; never printed back). In
 * both because either may run when make flash stores the PIN: the full firmware switches to setup at once without a
 * network (DESIGN.md 5.3.1).
 */
#include <stdio.h>
#include <string.h>

#include "easyflash.h"
#include "shell.h"

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
        printf("provision: %s none\r\n", key);
    else
        printf("provision: %s len %u crc %08lx\r\n", key, (unsigned)saved, (unsigned long)crc32(v, n));
}

static void store_pin(const char *pin)
{
    if (strlen(pin) != 6 || strspn(pin, "0123456789") != 6) {
        printf("provision: pin: 6 digits\r\n");
        return;
    }
    if (ef_set_env_blob("pair_pin", pin, 6) != EF_NO_ERR) {
        printf("provision: pin store failed\r\n");
        return;
    }
    printf("provision: pairing PIN stored (it takes effect at the next boot)\r\n");
}

static void cmd_provision(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "pin")) {
        store_pin(argv[2]);
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "erase")) {
        if (ef_del_env("wifi_ssid") != EF_NO_ERR || ef_del_env("wifi_psk") != EF_NO_ERR ||
            ef_del_env("wifi_ap") != EF_NO_ERR) {
            printf("provision: erase failed\r\n");     /* easyflash not up yet or a flash error */
            return;
        }
        printf("provision: credentials and remembered AP erased\r\n");
        return;
    }
    size_t saved = 0;
    char pin[7];
    (void)ef_get_env_blob("pair_pin", pin, sizeof(pin) - 1, &saved);
    printf("provision: pairing PIN %s\r\n", saved == 6 ? "stored" : "none (provision pin)");
    print_key("wifi_ssid");
    print_key("wifi_psk");
}
SHELL_CMD_EXPORT_ALIAS(cmd_provision, provision, stored Wi-Fi network: length and CRC only [erase | pin <6 digits>]);
