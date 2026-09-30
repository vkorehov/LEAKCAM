#define _GNU_SOURCE
#include "nnstore.h"
#include "change.h"
#include "refstore.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define NN_MAGIC   0x4E4E454Cu          /* "LENN" little-endian */
#define NN_VERSION 1u

struct nn_header {
    uint32_t magic, version, gen;
    uint32_t model_len, model_crc;
    float thr[2];
    uint32_t header_crc;                /* over everything above */
};

static void path_of(char *buf, size_t n, const char *dir, int slot)
{
    snprintf(buf, n, "%s/change.%d", dir, slot);
}

static int header_ok(const struct nn_header *h)
{
    return h->magic == NN_MAGIC && h->version == NN_VERSION && h->model_len > 0 &&
           h->model_len <= NN_MAX_LEN && h->thr[0] > 0 && h->thr[0] <= 1 && h->thr[1] > 0 &&
           h->thr[1] <= 1 &&
           refstore_crc32((const uint8_t *)h, offsetof(struct nn_header, header_crc)) == h->header_crc;
}

static int read_header(const char *path, struct nn_header *h)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int rc = refstore_read_all(fd, h, sizeof(*h)) == 0 && header_ok(h) ? 0 : -1;
    close(fd);
    return rc;
}

/* the model of one slot whose header is h: 0, or -1 (damaged) */
static int read_model(const char *path, const struct nn_header *h, struct nn_model *m)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    uint8_t *p = malloc(h->model_len);
    struct nn_header skip;
    int ok = p && refstore_read_all(fd, &skip, sizeof(skip)) == 0 &&
             refstore_read_all(fd, p, h->model_len) == 0 && refstore_crc32(p, h->model_len) == h->model_crc;
    close(fd);
    if (!ok) {
        free(p);
        return -1;
    }
    *m = (struct nn_model){ p, h->model_len, h->model_crc, { h->thr[0], h->thr[1] } };
    return 0;
}

/* slot headers: which slot is newest (-1 none), and the generation of the newest valid header */
static int newest(const char *dir, struct nn_header h[2], int valid[2], uint32_t *gen)
{
    int best = -1;
    for (int s = 0; s < 2; s++) {
        char path[256];
        path_of(path, sizeof(path), dir, s);
        valid[s] = read_header(path, &h[s]) == 0;
        /* wrap-safe "newer": a slot is at most one generation ahead of the other */
        if (valid[s] && (best < 0 || (int32_t)(h[s].gen - h[best].gen) > 0))
            best = s;
    }
    *gen = best >= 0 ? h[best].gen : 0;
    return best;
}

static int load_factory(const char *factory, struct nn_model *m)
{
    int fd = open(factory, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > (off_t)NN_MAX_LEN) {
        fprintf(stderr, "nnstore: no factory kmodel %s\n", factory);
        if (fd >= 0)
            close(fd);
        return -1;
    }
    uint8_t *p = malloc((size_t)st.st_size);
    int ok = p && refstore_read_all(fd, p, (size_t)st.st_size) == 0;
    close(fd);
    if (!ok) {
        free(p);
        return -1;
    }
    *m = (struct nn_model){ p, (size_t)st.st_size, refstore_crc32(p, (size_t)st.st_size),
                            { CHANGE_THRESHOLD, CHANGE_THRESHOLD } };
    return 0;
}

int nnstore_load(const char *dir, const char *factory, struct nn_model *m)
{
    struct nn_header h[2];
    int valid[2];
    uint32_t gen;
    int best = newest(dir, h, valid, &gen);
    /* the newest slot first, then the other one: a damaged model falls back a generation */
    for (int k = 0; best >= 0 && k < 2; k++) {
        int s = k == 0 ? best : !best;
        char path[256];
        path_of(path, sizeof(path), dir, s);
        if (valid[s] && read_model(path, &h[s], m) == 0)
            return 0;
        if (valid[s])
            fprintf(stderr, "nnstore: %s is damaged, ignoring it\n", path);
    }
    return load_factory(factory, m);
}

int nnstore_save(const char *dir, const struct nn_model *m)
{
    struct nn_header h[2];
    int valid[2];
    uint32_t gen;
    int best = newest(dir, h, valid, &gen);
    struct nn_header n = {
        .magic = NN_MAGIC, .version = NN_VERSION, .gen = best >= 0 ? gen + 1 : 1,
        .model_len = (uint32_t)m->len, .model_crc = m->crc, .thr = { m->thr[0], m->thr[1] },
    };
    n.header_crc = refstore_crc32((const uint8_t *)&n, offsetof(struct nn_header, header_crc));
    if (!header_ok(&n) || refstore_crc32(m->data, m->len) != m->crc)
        return -1;
    char path[256];
    path_of(path, sizeof(path), dir, best == 0 ? 1 : 0);
    return refstore_replace(dir, path, &n, sizeof(n), m->data, m->len);
}
