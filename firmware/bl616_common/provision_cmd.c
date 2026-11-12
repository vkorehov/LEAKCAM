/*
 * `provision` in both firmwares: the stored Wi-Fi network (length and CRC only), its erase, and the BLE pairing PIN written
 * once when the firmware is flashed (tools/power-bringup/provision_pin.py from make flash; never printed back). In
 * both because either may run when make flash stores the PIN: the full firmware switches to setup at once without a
 * network (DESIGN.md 5.3.1). Also the mTLS credentials (tls_link.h): the CA certificate, the board's certificate and
 * its private key, DER, written once by tools/power-bringup/provision_tls.py in 48-byte hex chunks (the shell's line is
 * 120 characters) checked by length and CRC-32 before they are stored; only their lengths and CRCs are ever printed.
 */
#include <stdio.h>
#include <stdlib.h>
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

/* `provision tls <ca|crt|key> begin <len>` / `... <hex>` (appended) / `... end <crc32 hex>`: one blob at a time */
static struct {
    char name[8];
    uint8_t *buf;
    size_t len, got;
} up;

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void tls_abort(const char *why)
{
    if (up.buf) {
        memset(up.buf, 0, up.len);   /* a key's bytes */
        free(up.buf);
    }
    memset(&up, 0, sizeof(up));
    printf("provision: tls %s\r\n", why);
}

static void provision_tls(int argc, char **argv)
{
    if (argc < 4 || (strcmp(argv[2], "ca") && strcmp(argv[2], "crt") && strcmp(argv[2], "key")) ||
        ((!strcmp(argv[3], "begin") || !strcmp(argv[3], "end")) && argc < 5)) {
        printf("provision: tls <ca|crt|key> begin <len> | <hex> | end <crc32>\r\n");
        return;
    }
    char key[8];
    snprintf(key, sizeof(key), "tls_%s", argv[2]);
    if (!strcmp(argv[3], "begin")) {
        if (up.buf)
            tls_abort("restarted");
        size_t n = strtoul(argv[4], NULL, 10);
        if (n == 0 || n > 1024 || !(up.buf = malloc(n))) {
            printf("provision: tls %s: length 1-1024\r\n", argv[2]);
            return;
        }
        snprintf(up.name, sizeof(up.name), "%s", key);
        up.len = n;
        printf("provision: tls %s begin %u\r\n", argv[2], (unsigned)n);
        return;
    }
    if (!up.buf || strcmp(up.name, key)) {
        printf("provision: tls %s: no begin\r\n", argv[2]);
        return;
    }
    if (!strcmp(argv[3], "end")) {
        uint32_t want = strtoul(argv[4], NULL, 16);
        if (up.got != up.len || crc32(up.buf, up.len) != want)
            return tls_abort("check failed: nothing stored");
        bool ok = ef_set_env_blob(key, up.buf, up.len) == EF_NO_ERR;
        size_t n = up.len;
        tls_abort(ok ? "stored" : "store failed");
        if (ok)
            printf("provision: %s len %u crc %08lx stored\r\n", key, (unsigned)n, (unsigned long)want);
        return;
    }
    const char *h = argv[3];
    size_t hl = strlen(h);
    if (hl % 2 || up.got + hl / 2 > up.len)
        return tls_abort("bad chunk: nothing stored");
    for (size_t i = 0; i < hl; i += 2) {
        int a = hexval(h[i]), b = hexval(h[i + 1]);
        if (a < 0 || b < 0)
            return tls_abort("bad chunk: nothing stored");
        up.buf[up.got++] = (uint8_t)(a << 4 | b);
    }
    printf("provision: tls %s %u/%u\r\n", argv[2], (unsigned)up.got, (unsigned)up.len);
}

static void print_blob(const char *key)
{
    uint8_t *v = malloc(1024);
    size_t saved = 0, n = v ? ef_get_env_blob(key, v, 1024, &saved) : 0;
    if (saved == 0)
        printf("provision: %s none\r\n", key);
    else
        printf("provision: %s len %u crc %08lx\r\n", key, (unsigned)saved, (unsigned long)crc32(v, n));
    if (v) {
        memset(v, 0, 1024);
        free(v);
    }
}

static void cmd_provision(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "tls")) {
        provision_tls(argc, argv);
        return;
    }
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
    print_blob("tls_ca");
    print_blob("tls_crt");
    print_blob("tls_key");
}
SHELL_CMD_EXPORT_ALIAS(cmd_provision, provision, stored network and credentials: length and CRC only [erase | pin <6 digits> | tls ...]);
