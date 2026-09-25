#include "k230_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflb_uart.h"

static char line[LINK_MAX_LINE];
static unsigned len;
static bool in_frame;

/* receive side: last accepted seq (-1 = none since the last READY / reset) and its answer */
static int rx_last = -1;
static bool rx_last_ok;
static bool rx_last_ready;         /* the last accepted command was READY */
static bool (*verdict_fn)(const struct link_msg *m);

/* transmit side: one command in flight, the rest queued behind it */
struct tx_cmd {
    char cmd[16];
    char arg[40];
};
static struct tx_cmd txq[LINK_QUEUE + 1];
static unsigned tx_head, tx_count;
static uint8_t tx_seq;              /* seq of the command in flight */
static uint8_t tx_next;             /* next seq to hand out */
static unsigned tx_sends;           /* sends of the command in flight, 0 = not sent yet */
static uint64_t tx_at;              /* time of the last send */
static struct link_result result;
static bool result_ready;

static struct bflb_device_s *uart(void)
{
    static struct bflb_device_s *u;
    if (!u)
        u = bflb_device_get_by_name("uart0");
    return u;
}

void link_reset(void)
{
    len = 0;
    in_frame = false;
    rx_last = -1;
    tx_head = tx_count = tx_sends = 0;
    tx_next = 0;
    result_ready = false;
}

void link_set_verdict(bool (*verdict)(const struct link_msg *m))
{
    verdict_fn = verdict;
}

static void send_frame(uint8_t seq, const char *cmd, const char *arg)
{
    char body[LINK_MAX_LINE], frame[LINK_MAX_LINE + 8];
    if (arg && *arg)
        snprintf(body, sizeof(body), "%u,%s,%s", seq, cmd, arg);
    else
        snprintf(body, sizeof(body), "%u,%s", seq, cmd);
    uint8_t sum = 0;
    for (char *p = body; *p; p++)
        sum ^= (uint8_t)*p;
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, sum);
    for (int i = 0; i < n; i++)
        bflb_uart_putchar(uart(), frame[i]);
}

static void send_answer(uint8_t seq, bool ok)
{
    char s[4];
    snprintf(s, sizeof(s), "%u", seq);
    send_frame(seq, ok ? "ACK" : "NAK", s);   /* an answer's own seq is not checked by anyone */
}

/* the command in flight has ended: drop it and keep how, for link_service() to report */
static void tx_done(enum link_end end)
{
    result.end = end;
    snprintf(result.cmd, sizeof(result.cmd), "%s", txq[tx_head].cmd);
    result_ready = true;
    tx_head = (tx_head + 1) % (LINK_QUEUE + 1);
    tx_count--;
    tx_sends = 0;
}

static bool parse(struct link_msg *out)
{
    char *star = strchr(line, '*');
    if (!star || star[1] == 0 || star[2] == 0)
        return false;
    uint8_t sum = 0;
    for (char *p = line; p < star; p++)
        sum ^= (uint8_t)*p;
    if (strtoul(star + 1, NULL, 16) != sum)
        return false;
    *star = 0;

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

/* a whole valid frame: consume ACKs and duplicates, acknowledge and deliver the rest */
static bool accept(struct link_msg *m)
{
    bool ack = strcmp(m->cmd, "ACK") == 0;
    if (ack || strcmp(m->cmd, "NAK") == 0) {
        if (tx_count && tx_sends && m->has_arg && m->arg == tx_seq)
            tx_done(ack ? LINK_ACKED : LINK_NAKED);
        return false;                   /* a stale answer is simply ignored */
    }
    bool ready = strcmp(m->cmd, "READY") == 0;
    /* a READY is a (re)started agent, whose seq starts over, unless it repeats the READY we just
     * accepted (our answer was lost). A restarted agent that happens to reuse that seq is taken
     * for a repeat once; its WAKE wait times out and its next READY, with seq + 1, gets through. */
    if (ready && !(rx_last == m->seq && rx_last_ready)) {
        rx_last = -1;
        tx_head = tx_count = tx_sends = 0;  /* whatever we had queued was for the old agent */
    }
    if (rx_last == m->seq) {            /* our answer was lost and it was sent again */
        send_answer(m->seq, rx_last_ok);
        return false;
    }
    rx_last = m->seq;
    rx_last_ready = ready;
    rx_last_ok = verdict_fn ? verdict_fn(m) : true;
    send_answer(m->seq, rx_last_ok);
    return rx_last_ok;
}

bool link_poll(struct link_msg *out)
{
    int c;
    while ((c = bflb_uart_getchar(uart())) >= 0) {
        if (c == '$') {             /* a new frame always restarts, noise before it is dropped */
            in_frame = true;
            len = 0;
            continue;
        }
        if (!in_frame)
            continue;
        if (c == '\r')
            continue;
        if (c == '\n') {
            in_frame = false;
            line[len] = 0;
            if (parse(out) && accept(out))
                return true;
            continue;
        }
        if (len >= sizeof(line) - 1) {  /* over-long garbage */
            in_frame = false;
            continue;
        }
        line[len++] = (char)c;
    }
    return false;
}

bool link_send_cmd(const char *cmd, const char *arg)
{
    if (tx_count > LINK_QUEUE)
        return false;
    struct tx_cmd *t = &txq[(tx_head + tx_count) % (LINK_QUEUE + 1)];
    snprintf(t->cmd, sizeof(t->cmd), "%s", cmd);
    snprintf(t->arg, sizeof(t->arg), "%s", arg ? arg : "");
    tx_count++;
    return true;
}

const struct link_result *link_service(uint64_t now_ms)
{
    if (result_ready) {
        result_ready = false;
        return &result;
    }
    if (!tx_count)
        return NULL;
    struct tx_cmd *t = &txq[tx_head];
    if (tx_sends && now_ms - tx_at < LINK_ACK_TIMEOUT_MS)
        return NULL;
    if (tx_sends >= LINK_TRIES) {
        tx_done(LINK_TIMED_OUT);
        result_ready = false;
        return &result;
    }
    if (!tx_sends)
        tx_seq = tx_next++;
    send_frame(tx_seq, t->cmd, t->arg);
    tx_sends++;
    tx_at = now_ms;
    return NULL;
}

bool link_busy(void)
{
    return tx_count != 0;
}
