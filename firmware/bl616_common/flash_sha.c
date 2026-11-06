#include "flash_sha.h"

#include <stdio.h>

#include "bflb_flash.h"
#include "bflb_sec_sha.h"

void flash_sha256(uint32_t addr, uint32_t len, char out[65])
{
    /* the engine reads both from memory past the cache, the input 32-aligned (the SDK's sec_eng_sha example):
     * cached statics gave a wrong sum for a right copy (test130) */
    ATTR_NOCACHE_NOINIT_RAM_SECTION __attribute__((aligned(32))) static uint8_t buf[512];
    ATTR_NOCACHE_NOINIT_RAM_SECTION static struct bflb_sha256_ctx_s ctx;
    uint8_t h[32];
    struct bflb_device_s *sha = bflb_device_get_by_name(BFLB_NAME_SEC_SHA);   /* "sha" asserted (test130) */

    bflb_sha_init(sha, SHA_MODE_SHA256);
    bflb_sha256_start(sha, &ctx);
    for (uint32_t o = 0; o < len; o += sizeof(buf)) {
        uint32_t n = len - o < sizeof(buf) ? len - o : sizeof(buf);
        (void)bflb_flash_read(addr + o, buf, n);   /* a failed read gives a wrong sum: the caller compares it */
        (void)bflb_sha256_update(sha, &ctx, buf, n);
    }
    bflb_sha256_finish(sha, &ctx, h);
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", h[i]);
}
