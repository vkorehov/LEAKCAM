/*
 * Pure logic behind evlog.c, kept free of SDK headers so the host tests can run it: a ring of
 * the last EVLOG_N events, guarded by a magic and a CRC so that RAM left by a power loss reads
 * as an empty log instead of garbage.
 */
#ifndef LEAKCAM_EVLOG_CALC_H
#define LEAKCAM_EVLOG_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "aon_state_calc.h"                 /* aon_crc32 */

#define EVLOG_N     32u
#define EVLOG_MAGIC 0x4C564554u             /* "TEVL" */

struct evlog_entry {
    uint32_t t;                 /* RTC seconds (40-bit count / 32768) */
    uint8_t ev;                 /* enum ev of evlog.h */
    int8_t arg;
    uint16_t val;
};

struct evlog {
    uint32_t magic;
    uint16_t head;              /* next slot to write */
    uint16_t count;             /* entries held, up to EVLOG_N */
    struct evlog_entry e[EVLOG_N];
    uint32_t crc;               /* CRC-32 over everything above */
};

static inline uint32_t evlog_crc(const struct evlog *l)
{
    return aon_crc32(l, offsetof(struct evlog, crc));
}

static inline bool evlog_intact(const struct evlog *l)
{
    return l->magic == EVLOG_MAGIC && l->head < EVLOG_N && l->count <= EVLOG_N &&
           l->crc == evlog_crc(l);
}

static inline void evlog_clear(struct evlog *l)
{
    memset(l, 0, sizeof(*l));
    l->magic = EVLOG_MAGIC;
    l->crc = evlog_crc(l);
}

/* appends, overwriting the oldest entry once full; a damaged log starts over */
static inline void evlog_push(struct evlog *l, uint32_t t, uint8_t ev, int8_t arg, uint16_t val)
{
    if (!evlog_intact(l))
        evlog_clear(l);
    l->e[l->head] = (struct evlog_entry){ t, ev, arg, val };
    l->head = (uint16_t)((l->head + 1) % EVLOG_N);
    if (l->count < EVLOG_N)
        l->count++;
    l->crc = evlog_crc(l);
}

/* i = 0 is the oldest entry held; NULL past the end */
static inline const struct evlog_entry *evlog_at(const struct evlog *l, unsigned i)
{
    if (i >= l->count)
        return NULL;
    return &l->e[(l->head + EVLOG_N - l->count + i) % EVLOG_N];
}

#endif
