#include "aon_state.h"
#include "aon_state_calc.h"

#include <string.h>

#include "bflb_rtc.h"
#include "bl616_hbn.h"
#include "log.h"

#include "evlog.h"

/* in HBN RAM, placed by the linker in .hbn_noinit (bl616_leakcam.ld.in): never loaded or zeroed at start-up */
static struct aon_block aon_store __attribute__((section(".hbn_noinit")));
static volatile struct aon_block *const aon = &aon_store;

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
    /* HBN RAM in RET (or SLP) mode corrupts an access right after any AON register write, 4-12 % of them
     * (tests/test021, 2026-10-22); the last sleep left RET set and the ROM, boot2 and board_init() wrote AON
     * registers since: RET off before the first access, on again only in aon_prepare_sleep() */
    uint32_t sram = BL_RD_REG(HBN_BASE, HBN_SRAM);
    BL_WR_REG(HBN_BASE, HBN_SRAM, sram & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    HBN_Enable_RTC_Counter();          /* sets bit 0 only: a running counter keeps its count */
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a)) {
        LOG_I("aon: no retained state (power loss or first boot)\r\n");
        memset(&a, 0, sizeof(a));
    } else if (a.clock_valid && !aon_rebase(&a, rtc_now())) {
        LOG_W("aon: RTC counter was reset, wall clock unknown until the next TIME\r\n");
        evlog_add(EV_CLOCK_LOST, 0, 0);
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
    int32_t corr = had ? (int32_t)(epoch - old) : 0;
    corr = corr > INT16_MAX ? INT16_MAX : corr < INT16_MIN ? INT16_MIN : corr;
    evlog_add(EV_CLOCK, had, (uint16_t)(int16_t)corr);
    if (had)
        LOG_I("aon: clock set, correction %d s\r\n", (int)(epoch - old));
    else
        LOG_I("aon: clock set to %u\r\n", (unsigned)epoch);
    return true;
}

int battery_get(void)
{
    struct aon_block a;
    load(&a);
    return aon_intact(&a) && a.bat_mv ? (int)a.bat_mv : -1;
}

void battery_set(unsigned mv)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a))
        memset(&a, 0, sizeof(a));
    a.bat_mv = mv;
    store(&a);
}

bool adc_cal_get(struct adc_cal *c)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a) || !a.adc_cal_valid)
        return false;
    c->coe = a.adc_coe;
    c->os1 = a.adc_os1;
    c->os2 = a.adc_os2;
    c->tsen = a.adc_tsen;
    return true;
}

void adc_cal_set(const struct adc_cal *c)
{
    struct aon_block a;
    load(&a);
    if (!aon_intact(&a))
        memset(&a, 0, sizeof(a));
    a.adc_cal_valid = 1;
    a.adc_coe = c->coe;
    a.adc_os1 = c->os1;
    a.adc_os2 = c->os2;
    a.adc_tsen = c->tsen;
    store(&a);
}

void aon_prepare_sleep(void)
{
    HBN_Set_HRAM_Ret();
}
