/*
 * Pure logic behind aon_state.c, kept free of SDK headers so the host tests can run it.
 *
 * Wall clock = epoch at a reference RTC count + (RTC count now - reference) / 32768. The HBN RTC
 * is a 40-bit counter at 32768 Hz: it wraps after 2^40 / 32768 s = 388 days, so the reference is
 * moved forward (rebased) at every boot, well inside one wrap.
 */
#ifndef LEAKCAM_AON_STATE_CALC_H
#define LEAKCAM_AON_STATE_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AON_RTC_HZ    32768u
#define AON_RTC_MASK  ((1ull << 40) - 1)
#define AON_MAGIC     0x4C43414Du          /* "LCAM" */
#define AON_VERSION   1u
/* earliest plausible time: 2026-01-01T00:00:00Z. A clock from before that is a bug or a
 * default value, never real, and is refused. */
#define AON_EPOCH_MIN 1767225600u

struct aon_block {
    uint32_t magic, version;
    uint32_t flags;             /* PF_* bits of main.c */
    uint32_t hum_wakes_left;
    uint32_t clock_valid;       /* 1 = epoch_ref/rtc_ref describe real time */
    uint32_t epoch_ref;         /* Unix seconds at rtc_ref */
    uint64_t rtc_ref;           /* 40-bit RTC count, 32768 Hz */
    uint32_t reserved;
    uint32_t crc;               /* CRC-32 over everything above */
};

static inline uint32_t aon_crc32(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static inline void aon_seal(struct aon_block *a)
{
    a->magic = AON_MAGIC;
    a->version = AON_VERSION;
    a->crc = aon_crc32(a, offsetof(struct aon_block, crc));
}

static inline bool aon_intact(const struct aon_block *a)
{
    return a->magic == AON_MAGIC && a->version == AON_VERSION &&
           a->crc == aon_crc32(a, offsetof(struct aon_block, crc));
}

/*
 * Counts elapsed from ref to now, or -1 when the counter must have been reset in between
 * (now behind ref, and ref not in the last quarter of the range where a wrap is the only
 * explanation).
 */
static inline int64_t aon_rtc_elapsed(uint64_t ref, uint64_t now)
{
    ref &= AON_RTC_MASK;
    now &= AON_RTC_MASK;
    if (now >= ref)
        return (int64_t)(now - ref);
    if (ref >= AON_RTC_MASK - (AON_RTC_MASK >> 2))
        return (int64_t)((now + AON_RTC_MASK + 1) - ref);       /* wrapped */
    return -1;
}

/* move the reference to now, keeping the sub-second remainder in rtc_ref. false = the clock
 * could not be carried forward (counter reset) and is now invalid. */
static inline bool aon_rebase(struct aon_block *a, uint64_t now)
{
    if (!a->clock_valid)
        return false;
    int64_t d = aon_rtc_elapsed(a->rtc_ref, now);
    if (d < 0) {
        a->clock_valid = 0;
        return false;
    }
    uint64_t secs = (uint64_t)d / AON_RTC_HZ;
    a->epoch_ref += (uint32_t)secs;
    a->rtc_ref = (a->rtc_ref + secs * AON_RTC_HZ) & AON_RTC_MASK;
    return true;
}

static inline bool aon_clock_now(const struct aon_block *a, uint64_t now, uint32_t *epoch)
{
    if (!a->clock_valid)
        return false;
    int64_t d = aon_rtc_elapsed(a->rtc_ref, now);
    if (d < 0)
        return false;
    *epoch = a->epoch_ref + (uint32_t)((uint64_t)d / AON_RTC_HZ);
    return true;
}

static inline bool aon_clock_set(struct aon_block *a, uint64_t now, uint32_t epoch)
{
    if (epoch < AON_EPOCH_MIN)
        return false;
    a->epoch_ref = epoch;
    a->rtc_ref = now & AON_RTC_MASK;
    a->clock_valid = 1;
    return true;
}

#endif
