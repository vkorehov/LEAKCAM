#include "flash_sha.h"

#include <stdio.h>

#include "bflb_flash.h"
#include "mbedtls/sha256.h"

void flash_sha256(uint32_t addr, uint32_t len, char out[65])
{
    /* through mbedtls: the SDK's port takes the SEC engine's mutex, which the supplicant shares; the engine driven
     * directly while Wi-Fi ran gave a wrong sum for a right copy (test130, the copy read back byte-exact) */
    static uint8_t buf[512];
    static mbedtls_sha256_context ctx;
    uint8_t h[32];

    mbedtls_sha256_init(&ctx);
    (void)mbedtls_sha256_starts_ret(&ctx, 0);
    for (uint32_t o = 0; o < len; o += sizeof(buf)) {
        uint32_t n = len - o < sizeof(buf) ? len - o : sizeof(buf);
        (void)bflb_flash_read(addr + o, buf, n);   /* a failed read gives a wrong sum: the caller compares it */
        (void)mbedtls_sha256_update_ret(&ctx, buf, n);
    }
    (void)mbedtls_sha256_finish_ret(&ctx, h);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", h[i]);
}
