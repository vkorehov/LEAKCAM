/* Host stand-in for the change net: the distance is LEAKCAM_TEST_CHANGE (default 1, a real change). */
#include <stdlib.h>

#include "change.h"

int change_load(const uint8_t *kmodel, size_t len)
{
    (void)kmodel; (void)len;
    return 0;
}

float change_distance(const uint8_t *ref, const uint8_t *cur)
{
    (void)ref; (void)cur;
    const char *s = getenv("LEAKCAM_TEST_CHANGE");
    return s ? (float)atof(s) : 1.0f;
}
