/*
 * The change net in use: a kmodel and its per-camera thresholds, updated by the server.
 *
 * An update is kept in two slot files, <dir>/change.0 and <dir>/change.1, as refstore keeps
 * references: a header (magic, version, generation, model length and CRC-32, the two camera
 * thresholds, header CRC-32) plus the kmodel. A save goes to the older slot; a load takes the
 * newest slot that checks. With no valid slot the factory kmodel (CHANGE_KMODEL, installed with
 * the programs) is used, with CHANGE_THRESHOLD for both cameras.
 */
#ifndef LEAKCAM_NNSTORE_H
#define LEAKCAM_NNSTORE_H

#include <stddef.h>
#include <stdint.h>

#define NN_MAX_LEN (4u << 20)          /* the largest kmodel accepted */

struct nn_model {
    uint8_t *data;                      /* malloc'd kmodel */
    size_t len;
    uint32_t crc;                       /* CRC-32 of the kmodel: its identity towards the server */
    float thr[2];                       /* change threshold of camera 0 and camera 1 */
};

/* the newest saved update, else the factory kmodel: 0, or -1 (nothing loadable, logged) */
int nnstore_load(const char *dir, const char *factory, struct nn_model *m);
/* m as the next update: 0 or -1 */
int nnstore_save(const char *dir, const struct nn_model *m);

#endif
