#include "device_id.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "bflb_efuse.h"

void device_id_bytes(uint8_t mac[6])
{
    uint8_t c[8];
    if (bflb_efuse_get_chipid(c) != 0)
        vAssertCalled();                     /* no factory MAC: no identity */
    for (int i = 0; i < 6; i++)
        mac[i] = c[i];
}

const char *device_id(void)
{
    static char id[13];
    if (!id[0]) {
        uint8_t m[6];
        device_id_bytes(m);
        snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    }
    return id;
}
