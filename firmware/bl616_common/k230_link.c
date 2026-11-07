/*
 * The BL616 side of the K230 link protocol (k230_link.h): frames answered ACK/NAK, one command of ours in flight with
 * resends, the K230 console text passed to the second USB port. Fed with the wire's bytes by k230_session.c.
 * Never logs a frame's contents: the USB setup lines carry the Wi-Fi credentials.
 */
#include "battery.h"
#include "k230_link.h"
#include "wall_clock.h"
#include "k230_linesplit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char line[LINK_MAX_LINE];
static unsigned len;
static bool frame_ready;

/* receive side: last accepted seq (-1 = none since the last READY / reset) and its answer */
static int rx_last = -1;
static uint8_t rx_last_code;       /* 0 = ACK, else the NAK code */
static bool rx_last_ready;         /* the last accepted command was READY */
static uint8_t (*reply_fn)(const struct link_msg *m);

/* transmit side: one command at a time */
static char tx_cmd[16], tx_arg[LINK_MAX_LINE];
static bool tx_pending;             /* a command is waiting to be sent or for its ACK */
static uint8_t tx_seq;              /* seq of the command in flight */
static uint8_t tx_next;             /* next seq to hand out */
static unsigned tx_sends;           /* sends of the command in flight, 0 = not sent yet */
static uint64_t tx_at;              /* time of the last send */
static struct link_result result;
static bool result_ready;

static struct linesplit split;
static bool split_ready;

void link_reset(void)
{
    len = 0;
    split_ready = false;
    frame_ready = false;
    rx_last = -1;
    tx_pending = false;
    tx_sends = 0;
    tx_next = 0;
    result_ready = false;
}

void link_set_reply(uint8_t (*reply)(const struct link_msg *m))
{
    reply_fn = reply;
}

static void send_frame(uint8_t seq, const char *cmd, const char *arg)
{
    char body[LINK_MAX_LINE + 16], frame[LINK_MAX_LINE + 24];
    int n = arg && *arg ? snprintf(body, sizeof(body), "%u,%s,%s", seq, cmd, arg)
                        : snprintf(body, sizeof(body), "%u,%s", seq, cmd);
    uint32_t t = wall_clock_now();
    if (t && n > 0 && (size_t)n < sizeof(body))
        snprintf(body + n, sizeof(body) - (size_t)n, ";t=%lu", (unsigned long)t);   /* the K230's clock (k230_link.h) */
    n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, link_crc8(body, body + strlen(body)));
    link_tx(frame, (unsigned)n);
}

static void send_answer(uint8_t seq, uint8_t code)
{
    char s[8];
    if (code)
        snprintf(s, sizeof(s), "%u,%u", seq, code);
    else
        snprintf(s, sizeof(s), "%u", seq);
    send_frame(seq, code ? "NAK" : "ACK", s);   /* an answer's own seq is not checked by anyone */
}

/* the command in flight has ended: drop it and keep how, for link_service() to report */
static void tx_done(enum link_end end, const char *answer)
{
    result.end = end;
    snprintf(result.cmd, sizeof(result.cmd), "%s", tx_cmd);
    snprintf(result.answer, sizeof(result.answer), "%s", answer);
    result_ready = true;
    tx_pending = false;
    tx_sends = 0;
}

static bool parse(struct link_msg *out)
{
    char *star = strchr(line, '*');
    if (!star || star[1] == 0 || star[2] == 0)
        return false;
    if (strtoul(star + 1, NULL, 16) != link_crc8(line, star))
        return false;
    *star = 0;
    /* the attributes after the last ';': the battery the K230 measured, on every frame (k230_link.h) */
    char *attr = strrchr(line, ';');
    if (attr && attr[1] == 'b' && attr[2] == '=') {
        *attr = 0;
        battery_seen((int)strtol(attr + 3, NULL, 10));
    }
    char *end;
    unsigned long seq = strtoul(line, &end, 10);
    if (end == line || *end != ',' || seq > 255)
        return false;
    char *cmd = end + 1, *comma = strchr(cmd, ',');
    if (comma)
        *comma = 0;
    if (!*cmd || strlen(cmd) >= sizeof(out->cmd))
        return false;
    strcpy(out->cmd, cmd);
    out->seq = (uint8_t)seq;
    out->has_arg = comma != NULL;
    out->arg = comma ? strtoul(comma + 1, NULL, 10) : 0;
    snprintf(out->args, sizeof(out->args), "%s", comma ? comma + 1 : "");
    return true;
}

/* a whole valid frame: ACK/NAK end ours, duplicates are answered again, the rest answered and delivered */
static bool accept(struct link_msg *m)
{
    bool ack = !strcmp(m->cmd, "ACK"), nak = !strcmp(m->cmd, "NAK");
    if (ack || nak) {
        if (tx_pending && tx_sends && m->has_arg && m->arg == tx_seq) {
            const char *extra = strchr(m->args, ',');     /* ACK,<seq>[,<answer>] */
            tx_done(ack ? LINK_ACKED : LINK_NAKED, extra ? extra + 1 : "");
        }
        return false;                   /* a stale answer is simply ignored */
    }
    bool ready = !strcmp(m->cmd, "READY");
    /* a READY is a (re)started program, whose seq starts over, unless it repeats the READY we just accepted (our
     * answer was lost) */
    if (ready && !(rx_last == m->seq && rx_last_ready)) {
        rx_last = -1;
        tx_pending = false;             /* whatever we were sending was for the old program */
        tx_sends = 0;
    }
    if (rx_last == m->seq) {            /* our answer was lost and it was sent again */
        send_answer(m->seq, rx_last_code);
        return false;
    }
    rx_last = m->seq;
    rx_last_ready = ready;
    rx_last_code = reply_fn ? reply_fn(m) : 0;
    send_answer(m->seq, rx_last_code);
    return rx_last_code == 0;
}

static void split_text(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    link_text(p, n);
}

static void split_frame(void *ctx, const char *l, unsigned n)
{
    (void)ctx;
    len = n - 1;                        /* without the '$' */
    memcpy(line, l + 1, len);
    line[len] = 0;
    frame_ready = true;
}

void link_rx(const char *p, unsigned n, void (*on_cmd)(const struct link_msg *m))
{
    if (!split_ready) {
        linesplit_init(&split, split_text, split_frame, NULL);
        split_ready = true;
    }
    for (unsigned i = 0; i < n; i++) {
        linesplit_feed(&split, &p[i], 1);
        if (!frame_ready)
            continue;
        frame_ready = false;
        struct link_msg m;
        if (parse(&m) && accept(&m) && on_cmd)
            on_cmd(&m);
    }
}

bool link_send_cmd(const char *cmd, const char *arg)
{
    if (tx_pending)
        return false;
    snprintf(tx_cmd, sizeof(tx_cmd), "%s", cmd);
    snprintf(tx_arg, sizeof(tx_arg), "%s", arg ? arg : "");
    tx_pending = true;
    return true;
}

const struct link_result *link_service(uint64_t now_ms)
{
    if (result_ready) {
        result_ready = false;
        return &result;
    }
    if (!tx_pending)
        return NULL;
    if (tx_sends && now_ms - tx_at < LINK_ACK_TIMEOUT_MS)
        return NULL;
    if (tx_sends >= LINK_TRIES) {
        tx_done(LINK_TIMED_OUT, "");
        result_ready = false;
        return &result;
    }
    if (!tx_sends)
        tx_seq = tx_next++;
    send_frame(tx_seq, tx_cmd, tx_arg);
    tx_sends++;
    tx_at = now_ms;
    return NULL;
}
