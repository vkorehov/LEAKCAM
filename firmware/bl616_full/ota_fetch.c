#include "ota_fetch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflb_flash.h"
#include "bflb_mtimer.h"
#include "easyflash.h"

#include "api.h"
#include "flash_sha.h"
#include "ota_env.h"
#include "wifi_check.h"

struct dl {
    uint32_t addr, size, off, erased;
};

/* the string value of "key":"..." in body: at most cap - 1 characters of [0-9A-Za-z.+-]; false if absent or longer */
static bool str_value(const char *body, const char *key, char *out, size_t cap)
{
    const char *p = strstr(body, key);
    if (!p)
        return false;
    p += strlen(key);
    size_t n = strspn(p, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.+-");
    if (n == 0 || n >= cap || p[n] != '"')
        return false;
    memcpy(out, p, n);
    out[n] = 0;
    return true;
}

static long num_value(const char *body, const char *key)
{
    const char *p = strstr(body, key);
    return p ? strtol(p + strlen(key), NULL, 10) : -1;
}

static void no_progress(enum wifi_check_state st, uint16_t detail)
{
    (void)st;
    (void)detail;
}

/* the download into the copy area: each sector erased as the copy reaches it, never past the offered length */
static bool sink(const uint8_t *data, size_t n, void *arg)
{
    struct dl *d = arg;
    if (n > d->size - d->off) {
        printf("ota: the download is longer than offered (%lu B)\r\n", (unsigned long)d->size);
        return false;
    }
    while (d->erased < d->off + n) {
        if (bflb_flash_erase(d->addr + d->erased, OTA_SECTOR) != 0)
            return false;
        d->erased += OTA_SECTOR;
    }
    if (bflb_flash_write(d->addr + d->off, (uint8_t *)data, n) != 0)
        return false;
    d->off += n;
    return true;
}

bool ota_fetch(const char *body, const char *ssid, size_t ssid_len, const char *psk, size_t psk_len)
{
    static char refused[48];                 /* one log line per version that does not fit, not one per check-in */
    char ver[48], sha[65], xz_sha[65], num[12], got[65];
    long size = num_value(body, "\"full_size\":"), xz_size = num_value(body, "\"xz_size\":");

    if (!str_value(body, "\"full\":\"", ver, sizeof(ver)))
        return false;                        /* no offer */
    if (!str_value(body, "\"full_sha\":\"", sha, sizeof(sha)) || strlen(sha) != 64 ||
        !str_value(body, "\"xz_sha\":\"", xz_sha, sizeof(xz_sha)) || strlen(xz_sha) != 64 || size <= 0 ||
        xz_size <= 0 || xz_size > OTA_SLOT0_LEN)
        return false;
    /* the copy must leave both the running image and the new one whole */
    uint32_t xz = ota_xz_addr((uint32_t)xz_size), own = 0;
    (void)bflb_flash_read(OTA_SLOT0 + OTA_HDR_IMG_LEN, (uint8_t *)&own, 4);
    own += OTA_SECTOR;                       /* the boot header itself */
    if (OTA_SLOT0 + own > xz || OTA_SLOT0 + (uint32_t)size > xz) {
        if (strcmp(refused, ver) != 0) {
            snprintf(refused, sizeof(refused), "%s", ver);
            printf("ota: %s does not fit: image %ld B + copy %ld B (running image %lu B) in slot 0's %u B\r\n", ver,
                   size, xz_size, (unsigned long)own, OTA_SLOT0_LEN);
        }
        return false;
    }
    struct dl d = { xz, (uint32_t)xz_size, 0, 0 };
    struct wifi_check_target t = { API_HOST, API_PORT, OTA_PATH, 200, NULL, 0, false, NULL, NULL, sink, &d };
    uint16_t detail = 0;
    printf("ota: full firmware %s offered: its copy (%ld B) to 0x%lx\r\n", ver, xz_size, (unsigned long)xz);
    uint64_t t0 = bflb_mtimer_get_time_ms();
    enum wifi_check_state st = wifi_check_run(ssid, ssid_len, psk, psk_len, &t, NULL, no_progress, &detail);
    uint32_t ms = (uint32_t)(bflb_mtimer_get_time_ms() - t0);
    if (st != WCHK_INTERNET || d.off != d.size) {
        printf("ota: download failed (state 0x%02x detail %u) after %lu of %lu B, %lu ms: the running firmware stays\r\n",
               st, detail, (unsigned long)d.off, (unsigned long)d.size, (unsigned long)ms);
        return false;
    }
    uint64_t t1 = bflb_mtimer_get_time_ms();
    flash_sha256(xz, d.size, got);
    if (strcmp(got, xz_sha) != 0) {
        printf("ota: the copy's SHA-256 %.16s... is not the offered %.16s...: the running firmware stays\r\n", got,
               xz_sha);
        return false;
    }
    printf("ota: copy %lu B in %lu ms, SHA-256 ok read back (%lu ms)\r\n", (unsigned long)d.size, (unsigned long)ms,
           (unsigned long)(bflb_mtimer_get_time_ms() - t1));
    bool kept = ef_set_env_blob(OTA_ENV_SHA, sha, 64) == EF_NO_ERR && ef_set_env_blob(OTA_ENV_XZ_SHA, xz_sha, 64) == EF_NO_ERR &&
                ef_set_env_blob(OTA_ENV_VER, ver, strlen(ver)) == EF_NO_ERR;
    snprintf(num, sizeof(num), "%ld", size);
    kept = kept && ef_set_env_blob(OTA_ENV_SIZE, num, strlen(num)) == EF_NO_ERR;
    snprintf(num, sizeof(num), "%ld", xz_size);
    kept = kept && ef_set_env_blob(OTA_ENV_XZ_SIZE, num, strlen(num)) == EF_NO_ERR;
    if (!kept)
        printf("ota: the upgrade's keys not stored: the running firmware stays\r\n");
    return kept;
}
