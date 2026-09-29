/* Host test of the retained event log: order before and after the ring wraps, the CRC guard
 * against RAM left by a power loss, and the 8-byte entry layout. */
#include <stdio.h>
#include <string.h>

#include "evlog_calc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    struct evlog l;
    CHECK(sizeof(struct evlog_entry) == 8, "entry is %zu bytes", sizeof(struct evlog_entry));

    /* garbage (power loss) is not a log; the first push starts a fresh one */
    memset(&l, 0xA5, sizeof(l));
    CHECK(!evlog_intact(&l), "garbage accepted");
    evlog_push(&l, 100, 1, -3, 1650);
    CHECK(evlog_intact(&l) && l.count == 1, "fresh log after garbage: count %u", l.count);
    const struct evlog_entry *e = evlog_at(&l, 0);
    CHECK(e && e->t == 100 && e->ev == 1 && e->arg == -3 && e->val == 1650, "entry kept");
    CHECK(!evlog_at(&l, 1), "entry past the end");

    /* fill past the ring: the oldest go, order stays oldest first */
    evlog_clear(&l);
    for (unsigned i = 0; i < EVLOG_N + 5; i++)
        evlog_push(&l, 1000 + i, (uint8_t)(i & 0x7F), 0, (uint16_t)i);
    CHECK(l.count == EVLOG_N, "count %u after wrap", l.count);
    CHECK(evlog_at(&l, 0)->val == 5, "oldest after wrap is %u", evlog_at(&l, 0)->val);
    CHECK(evlog_at(&l, EVLOG_N - 1)->val == EVLOG_N + 4, "newest after wrap is %u",
          evlog_at(&l, EVLOG_N - 1)->val);
    for (unsigned i = 1; i < EVLOG_N; i++)
        CHECK(evlog_at(&l, i)->t == evlog_at(&l, i - 1)->t + 1, "order broken at %u", i);

    /* one flipped bit anywhere (a brown-out mid-write) empties the log instead of lying */
    ((uint8_t *)&l)[40] ^= 0x10;
    CHECK(!evlog_intact(&l), "bit flip accepted");
    evlog_push(&l, 7, 2, 0, 0);
    CHECK(l.count == 1 && evlog_at(&l, 0)->t == 7, "damaged log not restarted");

    if (fails)
        return 1;
    printf("evlog: layout, wrap order, CRC guard against power-loss RAM and bit flips\n");
    return 0;
}
