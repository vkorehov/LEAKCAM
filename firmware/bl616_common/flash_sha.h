#pragma once

#include <stdint.h>

/* SHA-256 of len bytes of flash at addr (the hardware engine), as 64 lower-case hex digits + NUL: what the flash
 * holds, read back (the OTA's checks, ota_env.h). Not while Wi-Fi runs: the supplicant shares the engine. */
void flash_sha256(uint32_t addr, uint32_t len, char out[65]);
