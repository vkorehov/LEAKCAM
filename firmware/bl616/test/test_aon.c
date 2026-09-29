/* Host test of the always-on state logic: clock arithmetic across the 40-bit RTC wrap, counter
 * reset detection, rebasing without losing sub-seconds, and the CRC guard. */
#include <stdio.h>
#include <string.h>

#include "aon_state_calc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define HZ AON_RTC_HZ
#define T0 1790000000u                      /* 2026-09-21 */

int main(void)
{
    struct aon_block a;
    memset(&a, 0, sizeof(a));
    uint32_t e;

    /* fresh block: no clock, bogus times refused */
    CHECK(!aon_clock_now(&a, 1000, &e), "clock valid on a fresh block");
    CHECK(!aon_clock_set(&a, 1000, 12345), "1970 time accepted");
    CHECK(!aon_clock_set(&a, 1000, AON_EPOCH_MIN - 1), "2025 time accepted");

    /* set, then read 10.5 s later */
    uint64_t r = 5ull * HZ + 123;
    CHECK(aon_clock_set(&a, r, T0), "set");
    CHECK(aon_clock_now(&a, r + 10 * HZ + HZ / 2, &e) && e == T0 + 10, "10.5 s later: %u", e);

    /* one day of hibernate wakes, rebasing at every boot: no drift from rounding */
    uint64_t now = r;
    for (int i = 0; i < 144; i++) {
        now += 600ull * HZ + 7;             /* 600 s plus a few ticks of boot time each wake */
        CHECK(aon_rebase(&a, now), "rebase %d", i);
    }
    uint64_t total = now - r;
    CHECK(aon_clock_now(&a, now, &e) && e == T0 + total / HZ, "after 144 rebases: %u vs %llu", e,
          (unsigned long long)(T0 + total / HZ));

    /* 40-bit wrap: reference near the top, counter wraps past zero */
    struct aon_block w;
    memset(&w, 0, sizeof(w));
    uint64_t top = AON_RTC_MASK - 30ull * HZ;
    CHECK(aon_clock_set(&w, top, T0), "set near top");
    uint64_t after = (top + 100ull * HZ) & AON_RTC_MASK;   /* wrapped, small value */
    CHECK(after < top, "test setup: counter should have wrapped");
    CHECK(aon_clock_now(&w, after, &e) && e == T0 + 100, "across the wrap: %u", e);
    CHECK(aon_rebase(&w, after) && aon_clock_now(&w, after + HZ, &e) && e == T0 + 101,
          "rebase across the wrap: %u", e);

    /* counter reset (EN reset, or cleared by someone): now far behind a mid-range reference */
    struct aon_block z;
    memset(&z, 0, sizeof(z));
    CHECK(aon_clock_set(&z, 1000ull * HZ, T0), "set mid");
    CHECK(!aon_rebase(&z, 5 * HZ) && !z.clock_valid, "counter reset not detected");
    CHECK(!aon_clock_now(&z, 6 * HZ, &e), "invalid clock still reports time");

    /* CRC guard: sealed block intact, any flipped byte rejected */
    aon_seal(&a);
    CHECK(aon_intact(&a), "sealed block rejected");
    a.epoch_ref ^= 1;
    CHECK(!aon_intact(&a), "corrupted block accepted");
    struct aon_block junk;
    memset(&junk, 0xA5, sizeof(junk));
    CHECK(!aon_intact(&junk), "random RAM accepted");

    printf(fails ? "aon: %d failures\n"
                 : "aon: clock set/read, 144 rebases without drift, 40-bit wrap, counter reset, CRC\n",
           fails);
    return fails != 0;
}
