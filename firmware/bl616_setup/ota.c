#include "ota.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_flash.h"
#include "bflb_mtimer.h"
#include "easyflash.h"
#include "xz.h"

#include "flash_sha.h"
#include "metrics.h"
#include "ota_env.h"
#include "slot.h"

#define POOL     (72 * 1024)                 /* the decoder: 32 KiB dictionary (the SDK's .xz) + ~28 KiB of tables */
#define RETRY_S  60                          /* a failed unpack: again from the intact copy */

void simple_malloc_init(uint8_t *buf, uint32_t len);   /* the SDK decoder's allocator (lzma/xz/xz_port.c) */

static bool env_present(const char *key)
{
    char c;
    size_t n = 0;
    (void)ef_get_env_blob(key, &c, 1, &n);
    return n > 0;
}

bool ota_blocks_full(void)
{
    return env_present(OTA_ENV_SHA) || env_present(OTA_ENV_BROKEN);
}

static long env_num(const char *key)
{
    char s[12] = { 0 };
    (void)ef_get_env_blob(key, s, sizeof(s) - 1, NULL);
    return strtol(s, NULL, 10);
}

static bool clear(void)
{
    return ef_del_env(OTA_ENV_SHA) == EF_NO_ERR && ef_del_env(OTA_ENV_SIZE) == EF_NO_ERR &&
           ef_del_env(OTA_ENV_XZ_SHA) == EF_NO_ERR && ef_del_env(OTA_ENV_XZ_SIZE) == EF_NO_ERR &&
           ef_del_env(OTA_ENV_VER) == EF_NO_ERR && ef_del_env(OTA_ENV_BROKEN) == EF_NO_ERR;
}

/* the copy unpacked over slot 0 from its start, each sector erased as the image reaches it; true: size bytes written
 * (the SDK's .xz pads the image by 16 bytes: not written) */
static bool unpack(uint32_t xz, uint32_t xz_size, uint32_t size)
{
    static uint8_t in[512], out[OTA_SECTOR];
    uint8_t *pool = pvPortMalloc(POOL);
    if (!pool) {
        printf("ota: no %u B for the decoder\r\n", POOL);
        return false;
    }
    simple_malloc_init(pool, POOL);
    xz_crc32_init();
    struct xz_dec *s = xz_dec_init(XZ_DYNALLOC, 1 << 15);
    struct xz_buf b = { in, 0, 0, out, 0, sizeof(out) };
    uint32_t read = 0, written = 0;
    enum xz_ret r = XZ_OK;
    while (s && r == XZ_OK) {
        if (b.in_pos == b.in_size && read < xz_size) {
            b.in_size = xz_size - read < sizeof(in) ? xz_size - read : sizeof(in);
            (void)bflb_flash_read(xz + read, in, b.in_size);   /* checked by its SHA-256 before, by the result after */
            b.in_pos = 0;
            read += b.in_size;
        }
        r = xz_dec_run(s, &b);
        if (b.out_pos == b.out_size || r == XZ_STREAM_END) {
            uint32_t n = b.out_pos;
            if (n > size - written)
                n = size - written;          /* the padding after the image */
            if (n && (written % OTA_SECTOR != 0 || bflb_flash_erase(OTA_SLOT0 + written, OTA_SECTOR) != 0 ||
                      bflb_flash_write(OTA_SLOT0 + written, out, n) != 0)) {
                printf("ota: write at 0x%lx failed\r\n", (unsigned long)(OTA_SLOT0 + written));
                r = XZ_DATA_ERROR;
                break;
            }
            written += n;
            b.out_pos = 0;
        }
    }
    if (s)
        xz_dec_end(s);
    vPortFree(pool);
    if (r != XZ_STREAM_END || written != size) {
        printf("ota: unpack stopped (xz %d) at %lu of %lu B\r\n", r, (unsigned long)written, (unsigned long)size);
        return false;
    }
    return true;
}

/* true: slot 0 holds the new image, checked; the switch follows */
static bool run(void)
{
    char sha[65] = { 0 }, xz_sha[65] = { 0 }, ver[48] = { 0 }, got[65];
    long size = env_num(OTA_ENV_SIZE), xz_size = env_num(OTA_ENV_XZ_SIZE);
    (void)ef_get_env_blob(OTA_ENV_SHA, sha, 64, NULL);
    (void)ef_get_env_blob(OTA_ENV_XZ_SHA, xz_sha, 64, NULL);
    (void)ef_get_env_blob(OTA_ENV_VER, ver, sizeof(ver) - 1, NULL);
    uint32_t xz = ota_xz_addr((uint32_t)xz_size);
    if (strlen(sha) != 64 || size <= 0 || xz_size <= 0 || OTA_SLOT0 + (uint32_t)size > xz) {
        printf("ota: no usable upgrade stored (slot 0 %s)\r\n", env_present(OTA_ENV_BROKEN) ? "half written" : "intact");
        return false;
    }
    uint64_t t0 = bflb_mtimer_get_time_ms();
    flash_sha256(xz, (uint32_t)xz_size, got);
    if (strcmp(got, xz_sha) != 0) {
        /* the copy changed since the full firmware checked it: nothing written yet, back there; it fetches again */
        printf("ota: the copy's SHA-256 %.16s... is not %.16s...\r\n", got, xz_sha);
        if (!env_present(OTA_ENV_BROKEN) && clear())
            return true;
        return false;
    }
    if (ef_set_env_blob(OTA_ENV_BROKEN, "1", 1) != EF_NO_ERR) {   /* before slot 0's first erase */
        printf("ota: %s not stored: no write\r\n", OTA_ENV_BROKEN);
        return false;
    }
    printf("ota: full firmware %s (%ld B) unpacked over slot 0\r\n", ver, size);
    if (!unpack(xz, (uint32_t)xz_size, (uint32_t)size))
        return false;
    uint32_t ms = (uint32_t)(bflb_mtimer_get_time_ms() - t0);
    flash_sha256(OTA_SLOT0, (uint32_t)size, got);
    if (strcmp(got, sha) != 0) {
        printf("ota: slot 0's SHA-256 %.16s... is not %.16s...\r\n", got, sha);
        return false;
    }
    printf("ota: slot 0 written and checked in %lu ms\r\n", (unsigned long)ms);
    if (!clear()) {
        printf("ota: the upgrade's keys not cleared: no switch\r\n");
        return false;
    }
    return true;
}

static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (run()) {
            printf("ota: to the full firmware\r\n");
            metrics_switch();                /* counted as every switch (sw in the check-in, test130 step 13) */
            if (slot_switch() != 0)          /* returns only on a failed partition-table write */
                printf("ota: slot switch failed\r\n");
        } else if (!env_present(OTA_ENV_SHA)) {
            break;                           /* nothing left to unpack from: the board stays here */
        }
        vTaskDelay(pdMS_TO_TICKS(RETRY_S * 1000));
    }
    vTaskDelete(NULL);
}

bool ota_start(void)
{
    if (!ota_blocks_full())
        return false;
    if (xTaskCreate(ota_task, "ota", 1024, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
    return true;
}
