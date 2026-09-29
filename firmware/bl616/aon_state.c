#include "aon_state.h"
#include "aon_state_calc.h"

#include <string.h>

#include "bflb_rtc.h"
#include "bl616_hbn.h"
#include "log.h"

/* the last 64 bytes of HBN RAM, away from anything the SDK might place at its start */
#define AON_ADDR (HBN_RAM_BASE + HBN_RAM_SIZE - 64u)
_Static_assert(sizeof(struct aon_block) <= 64, "aon block must fit the reserved 64 bytes");

static volatile struct aon_block *const aon = (volatile struct aon_block *)AON_ADDR;

static uint64_t rtc_now(void)
{
    return bflb_rtc_get_time(NULL) & AON_RTC_MASK;
}

static void load(struct aon_block *a)
{
    memcpy(a, (const void *)aon, sizeof(*a));
}

static void store(struct aon_block *a)
{
    aon_seal(a);
    memcpy((void *)aon, a, sizeof(*a));
}

void aon_init(void)
{
    HBN_Set_HRAM_Ret();
    HBN_Enable_RTC_Counter();          /* sets bit 0 only: a running counter keeps its count */
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a)) {
        LOG_I("aon: no retained state (power loss or first boot)\r\n");
        memset(&a, 0, sizeof(a));
    } else if (a.clock_valid && !aon_rebase(&a, rtc_now())) {
        LOG_W("aon: RTC counter was reset, wall clock unknown until the next TIME\r\n");
    }
    store(&a);
}

void persist_get(uint32_t *flags, uint32_t *hum_wakes_left)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a))
        memset(&a, 0, sizeof(a));
    *flags = a.flags & 0xFFu;
    *hum_wakes_left = a.hum_wakes_left;
}

void persist_set(uint32_t flags, uint32_t hum_wakes_left)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a))
        memset(&a, 0, sizeof(a));
    a.flags = flags & 0xFFu;
    a.hum_wakes_left = hum_wakes_left;
    store(&a);
}

bool wallclock_get(uint32_t *epoch)
{
    struct aon_block a;
    load(&a);
    return aon_intact(&a) && aon_clock_now(&a, rtc_now(), epoch);
}

bool wallclock_set(uint32_t epoch)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a))
        memset(&a, 0, sizeof(a));
    uint32_t old;
    bool had = aon_clock_now(&a, rtc_now(), &old);
    if (!aon_clock_set(&a, rtc_now(), epoch))
        return false;
    store(&a);
    if (had)
        LOG_I("aon: clock set, correction %d s\r\n", (int)(epoch - old));
    else
        LOG_I("aon: clock set to %u\r\n", (unsigned)epoch);
    return true;
}

void aon_prepare_sleep(void)
{
    HBN_Set_HRAM_Ret();
}
