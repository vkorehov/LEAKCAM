/* Host tests for the image history: keyframe + deltas round trip, policy, quota, damage. */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "history.h"

#define W 1280
#define H 960
#define BW (W / IMGDIFF_BLOCKS_X)
#define BH (H / IMGDIFF_BLOCKS_Y)
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned seed = 7;
static uint8_t rnd(void) { seed = seed * 1103515245u + 12345u; return (uint8_t)(seed >> 16); }

/* a textured frame: gradient plus noise, like a real scene (compresses, but not to nothing) */
static void frame(uint8_t *img, int shift)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (uint8_t)(40 + (x + y + shift) / 16 + (rnd() & 7));
}

static void paint_block(uint8_t *img, int bx, int by, uint8_t v)
{
    for (int y = by * BH; y < (by + 1) * BH; y++)
        memset(img + y * W + bx * BW, v, BW);
}

static void rm_tree(const char *dir)
{
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        printf("warning: could not remove %s\n", dir);
}

static int rebuild_equals(const char *dir, uint32_t seq, const uint8_t *want)
{
    uint8_t *got;
    unsigned w, h;
    time_t t;
    if (hist_rebuild(dir, 0, seq, &got, &w, &h, &t) != 0)
        return 0;
    int ok = w == W && h == H && !memcmp(got, want, (size_t)W * H);
    free(got);
    return ok;
}

int main(void)
{
    char dir[] = "/tmp/leakcam-histXXXXXX";
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    struct hist_cfg cfg;
    hist_default_cfg(&cfg, dir);
    struct hist_info hi;
    uint8_t chg[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X];
    uint8_t *f0 = malloc(W * H), *f1 = malloc(W * H), *f2 = malloc(W * H), *view = malloc(W * H);

    /* 1. first frame: keyframe whatever the mask says */
    frame(f0, 0);
    memset(chg, 0, sizeof(chg));
    CHECK(hist_store(&cfg, 0, 1000, f0, W, H, chg, 150, 0, &hi) == 0, "store first");
    CHECK(hi.kind == HIST_KEY && hi.seq == 1, "first record is not keyframe 1 (kind %c seq %u)", hi.kind, hi.seq);
    printf("keyframe:            %7zu bytes (raw %d)\n", hi.bytes, W * H);

    /* 2. nothing changed: nothing stored */
    CHECK(hist_store(&cfg, 0, 1600, f0, W, H, chg, 150, 0, &hi) == 0 && hi.kind == HIST_NONE,
          "unchanged frame stored something");

    /* 3. two blocks changed: delta with exactly those blocks; the rebuild shows them, the other
     *    blocks keep the keyframe even though the new frame differs there a little (noise) */
    frame(f1, 0);
    paint_block(f1, 3, 4, 200);
    paint_block(f1, 10, 7, 20);
    chg[4][3] = chg[7][10] = 1;
    CHECK(hist_store(&cfg, 0, 2200, f1, W, H, chg, 150, 0, &hi) == 0, "store delta");
    CHECK(hi.kind == HIST_DELTA && hi.seq == 2 && hi.key_seq == 1 && hi.nblocks == 2,
          "delta: kind %c seq %u key %u blocks %u", hi.kind, hi.seq, hi.key_seq, hi.nblocks);
    printf("delta, 2 blocks:     %7zu bytes\n", hi.bytes);
    memcpy(view, f0, W * H);
    for (int y = 4 * BH; y < 5 * BH; y++)
        memcpy(view + y * W + 3 * BW, f1 + y * W + 3 * BW, BW);
    for (int y = 7 * BH; y < 8 * BH; y++)
        memcpy(view + y * W + 10 * BW, f1 + y * W + 10 * BW, BW);
    CHECK(rebuild_equals(dir, 2, view), "rebuild of the delta differs from keyframe + blocks");
    CHECK(rebuild_equals(dir, 0, view), "newest (seq 0) is not the delta");
    CHECK(rebuild_equals(dir, 1, f0), "keyframe no longer rebuilds on its own");

    /* 4. a second delta on top of the first */
    memset(chg, 0, sizeof(chg));
    memcpy(f2, f1, W * H);
    paint_block(f2, 3, 4, 90);
    chg[4][3] = 1;
    CHECK(hist_store(&cfg, 0, 2800, f2, W, H, chg, 150, 0, &hi) == 0 && hi.kind == HIST_DELTA &&
          hi.key_seq == 1, "second delta");
    for (int y = 4 * BH; y < 5 * BH; y++)
        memcpy(view + y * W + 3 * BW, f2 + y * W + 3 * BW, BW);
    CHECK(rebuild_equals(dir, 3, view), "two deltas do not stack");

    /* 5. more than key_frac of the usable blocks changed: keyframe */
    memset(chg, 1, sizeof(chg));
    frame(f1, 500);
    CHECK(hist_store(&cfg, 0, 3400, f1, W, H, chg, 150, 0, &hi) == 0 && hi.kind == HIST_KEY &&
          hi.seq == 4, "big change did not start a keyframe (kind %c)", hi.kind);
    CHECK(rebuild_equals(dir, 4, f1), "second keyframe");
    CHECK(rebuild_equals(dir, 3, view), "older group lost after a new keyframe");

    /* 6. max_deltas: the next delta after the limit becomes a keyframe */
    cfg.max_deltas = 2;
    memset(chg, 0, sizeof(chg));
    chg[0][0] = 1;
    enum hist_kind k[3];
    for (int i = 0; i < 3; i++) {
        CHECK(hist_store(&cfg, 0, 4000 + i, f1, W, H, chg, 150, 0, &hi) == 0, "store %d", i);
        k[i] = hi.kind;
    }
    CHECK(k[0] == HIST_DELTA && k[1] == HIST_DELTA && k[2] == HIST_KEY,
          "max_deltas not honoured (%c %c %c)", k[0], k[1], k[2]);
    cfg.max_deltas = 96;

    /* 7. damage: a torn delta makes that frame (and later ones in the group) unavailable,
     *    the keyframe and earlier frames still rebuild */
    char path[400];
    snprintf(path, sizeof(path), "%s/hist0/%08u.D", dir, 3u);
    struct stat st;
    CHECK(stat(path, &st) == 0 && st.st_size > 60, "delta size");
    CHECK(truncate(path, st.st_size / 2) == 0, "truncate");   /* torn in the middle */
    uint8_t *got;
    unsigned w, h;
    time_t t;
    CHECK(hist_rebuild(dir, 0, 3, &got, &w, &h, &t) == 1, "torn delta accepted");
    CHECK(hist_rebuild(dir, 0, 2, &got, &w, &h, &t) == 0, "record before the torn one lost");
    free(got);

    /* 8. stale .tmp from a power cut is cleaned up and ignored */
    snprintf(path, sizeof(path), "%s/hist0/%08u.K.tmp", dir, 99u);
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs("partial", fp);
        fclose(fp);
    }
    struct hist_entry *e;
    size_t n;
    CHECK(hist_list(dir, 0, &e, &n) == 0, "list");
    CHECK(access(path, F_OK) != 0, "stale .tmp not removed");
    printf("records listed:      %zu (torn one skipped)\n", n);
    free(e);

    /* 9. quota: oldest whole groups go, the newest group always stays */
    cfg.quota_bytes = 1;                                  /* everything over quota */
    memset(chg, 0, sizeof(chg));
    chg[1][1] = 1;
    CHECK(hist_store(&cfg, 0, 5000, f1, W, H, chg, 150, 0, &hi) == 0 && hi.pruned > 0,
          "quota did not prune");
    CHECK(hist_list(dir, 0, &e, &n) == 0, "list after prune");
    int only_newest = n >= 1 && e[0].kind == HIST_KEY;
    for (size_t i = 1; i < n; i++)
        only_newest &= e[i].kind == HIST_DELTA && e[i].key_seq == e[0].seq;
    CHECK(only_newest, "after pruning, the remaining records are not one intact group (%zu left)", n);
    printf("after quota prune:   %zu records, %u files deleted\n", n, hi.pruned);
    CHECK(hist_rebuild(dir, 0, 0, &got, &w, &h, &t) == 0, "newest frame lost to pruning");
    free(got);
    free(e);

    rm_tree(dir);
    free(f0);
    free(f1);
    free(f2);
    free(view);
    printf(fails ? "history: %d failures\n" : "history: keyframe/delta round trip, policy, damage and quota handled\n", fails);
    return fails != 0;
}
