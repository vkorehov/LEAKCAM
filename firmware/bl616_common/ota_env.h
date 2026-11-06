#pragma once

#include <stdint.h>

/* The full firmware's upgrade (DESIGN.md 5.3.7, tests/test130). The full firmware, while it runs, downloads the
 * compressed new image (.xz) into the free end of its own slot 0 and checks it (bl616_full/ota_fetch.c); only then the
 * setup firmware unpacks it over the old image (bl616_setup/ota.c), with no network: a lost Wi-Fi never leaves the
 * board without a full firmware, a power cut restarts the unpack from the intact copy. Handed over in the key-value
 * store. */
#define OTA_ENV_SHA     "ota_sha"       /* the image's SHA-256 (64 lower-case hex digits): what slot 0 must hold */
#define OTA_ENV_SIZE    "ota_size"      /* the image's length, decimal */
#define OTA_ENV_XZ_SHA  "ota_xz_sha"    /* the .xz copy's SHA-256 */
#define OTA_ENV_XZ_SIZE "ota_xz_size"   /* its length, decimal */
#define OTA_ENV_VER     "ota_ver"       /* the version, for the log */
#define OTA_ENV_BROKEN  "full_broken"   /* set before slot 0's first erase, cleared once the new image checked */
#define OTA_PATH        "/v1/images/bl616_full"

#define OTA_SLOT0       0x10000         /* the full firmware's slot: the partition table's FW, address 0 */
#define OTA_SLOT0_LEN   0x200000
#define OTA_SECTOR      4096
#define OTA_HDR_IMG_LEN 0x84            /* the boot header's image length (the image after its 4 kB header) */

/* where the .xz copy of xz_len bytes sits: sector aligned at the slot's end */
static inline uint32_t ota_xz_addr(uint32_t xz_len)
{
    return OTA_SLOT0 + OTA_SLOT0_LEN - ((xz_len + OTA_SECTOR - 1) / OTA_SECTOR) * OTA_SECTOR;
}
