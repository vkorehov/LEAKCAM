/*
 * Pure logic behind evlog.c (no SDK headers: a host test can run it): a ring of the last EVLOG_N events, guarded by a
 * magic and a CRC so that RAM left by a power loss reads as an empty log, not garbage. Ported from the first product
 * firmware (attic/firmware/bl616/evlog_calc.h, DESIGN.md 5.2).
 */
#ifndef LEAKCAM_EVLOG_CALC_H
#define LEAKCAM_EVLOG_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EVLOG_N     44u                     /* 48 until 2026-11-10: 48 B of HBN RAM for the sleep schedule (wall_clock, climate, hbn_sleep) */
#define EVLOG_MAGIC 0x344C5645u             /* "EVL4" (44 entries); "EVL3", "EVL2" and the attic's "TEVL" read as empty */

struct evlog_entry {
    uint32_t ms;                /* uptime when it happened (restarts at every boot: EV_BOOT entries mark them) */
    uint8_t ev;                 /* enum ev of evlog.h */
    int8_t arg;
    uint16_t pad;
    uint32_t val;
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
    const uint8_t *p = (const uint8_t *)l;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < offsetof(struct evlog, crc); i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static inline bool evlog_intact(const struct evlog *l)
{
    return l->magic == EVLOG_MAGIC && l->head < EVLOG_N && l->count <= EVLOG_N && l->crc == evlog_crc(l);
}

static inline void evlog_clear(struct evlog *l)
{
    memset(l, 0, sizeof(*l));
    l->magic = EVLOG_MAGIC;
    l->crc = evlog_crc(l);
}

/* appends, overwriting the oldest entry once full; a damaged log starts over */
static inline void evlog_push(struct evlog *l, uint32_t ms, uint8_t ev, int8_t arg, uint32_t val)
{
    if (!evlog_intact(l))
        evlog_clear(l);
    l->e[l->head] = (struct evlog_entry){ ms, ev, arg, 0, val };
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
