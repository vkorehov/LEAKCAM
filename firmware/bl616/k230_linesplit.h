/*
 * The K230 <-> BL616 UART carries two streams on one wire (one rate, LINK_BAUD): link frames and the K230's console.
 * This splitter tells them apart, byte by byte, on both ends (BL616: K230 output -> link code + the K230 console USB
 * port; K230 kernel link_mux: BL616 output -> /dev/link + the msh console):
 *
 *   a frame   '$' ... '*' XX '\n', at most LINK_MAX_LINE bytes, with a correct CRC-8 (k230_link.h)  -> frame()
 *   all else  console text, passed on as it comes (a '$' that does not end in a valid frame is text too) -> text()
 *
 * The CRC is what keeps console output that happens to contain '$' from being taken as a frame. Text is passed on
 * at once, byte runs as they arrive (keystrokes must not wait for a newline); only a '$'-candidate is held back
 * until it is complete or proven not to be a frame. Header-only, no OS: the BL616 firmwares, the K230 kernel
 * driver and the host test (test/test_linesplit.c) share it.
 */
#ifndef LEAKCAM_K230_LINESPLIT_H
#define LEAKCAM_K230_LINESPLIT_H

#include <stdbool.h>
#include <stdint.h>

#include "k230_link.h"

typedef void (*linesplit_text_fn)(void *ctx, const char *p, unsigned n);
typedef void (*linesplit_frame_fn)(void *ctx, const char *line, unsigned n);   /* "$...*XX", no '\n' */

struct linesplit {
    char buf[LINK_MAX_LINE];
    unsigned n;                         /* bytes of the '$'-candidate held in buf, 0 = none */
    linesplit_text_fn text;
    linesplit_frame_fn frame;
    void *ctx;
};

static inline void linesplit_init(struct linesplit *s, linesplit_text_fn text, linesplit_frame_fn frame, void *ctx)
{
    s->n = 0;
    s->text = text;
    s->frame = frame;
    s->ctx = ctx;
}

static inline int linesplit_hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* "$<body>*XX" with XX = link_crc8(body) */
static inline bool linesplit_valid(const char *b, unsigned n)
{
    if (n < 5 || b[0] != '$' || b[n - 3] != '*')
        return false;
    int hi = linesplit_hex(b[n - 2]), lo = linesplit_hex(b[n - 1]);
    return hi >= 0 && lo >= 0 && link_crc8(b + 1, b + n - 3) == (uint8_t)(hi << 4 | lo);
}

static inline void linesplit_feed(struct linesplit *s, const char *p, unsigned n)
{
    const char *run = p;                /* start of the plain text run not yet passed on */
    for (unsigned i = 0; i < n; i++) {
        char c = p[i];
        if (!s->n) {
            if (c != '$')
                continue;               /* text: stays in the run */
            if (&p[i] > run)
                s->text(s->ctx, run, (unsigned)(&p[i] - run));
            s->buf[0] = '$';
            s->n = 1;
            run = &p[i + 1];
            continue;
        }
        /* inside a '$'-candidate */
        if (c == '\n') {
            if (linesplit_valid(s->buf, s->n)) {
                s->frame(s->ctx, s->buf, s->n);
            } else {
                s->text(s->ctx, s->buf, s->n);
                s->text(s->ctx, "\n", 1);
            }
            s->n = 0;
            run = &p[i + 1];
        } else if (c == '$' || s->n == sizeof(s->buf)) {
            /* not a frame after all: what was held is text; a '$' may start the next candidate */
            s->text(s->ctx, s->buf, s->n);
            s->n = 0;
            run = &p[i];
            i--;                        /* look at this byte again outside a candidate */
        } else {
            s->buf[s->n++] = c;
            run = &p[i + 1];
        }
    }
    if (!s->n && &p[n] > run)
        s->text(s->ctx, run, (unsigned)(&p[n] - run));
}

#endif
