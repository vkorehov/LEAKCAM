/* Host test: BL616 k230_link.c against frames built exactly like leakcam_agent.c send_frame():
 * parsing and noise, ACK/NAK answers, duplicates, resends and giving up, stale answers, READY. */
#include <stdio.h>
#include <string.h>
#include "k230_link.h"
#include "bflb_uart.h"

static struct bflb_device_s dev;
static char rx[1024];
static size_t rxi, rxn;
static char tx[2048];
static size_t txn;
struct bflb_device_s *bflb_device_get_by_name(const char *n) { (void)n; return &dev; }
int bflb_uart_getchar(struct bflb_device_s *d) { (void)d; return rxi < rxn ? (unsigned char)rx[rxi++] : -1; }
int bflb_uart_putchar(struct bflb_device_s *d, int c) { (void)d; if (txn < sizeof(tx) - 1) { tx[txn++] = (char)c; tx[txn] = 0; } return 0; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the K230 agent's framing, as leakcam_agent.c send_frame() */
static void agent_frame(char *out, unsigned seq, const char *cmd, const char *arg)
{
    char body[64];
    if (arg && *arg)
        snprintf(body, sizeof(body), "%u,%s,%s", seq, cmd, arg);
    else
        snprintf(body, sizeof(body), "%u,%s", seq, cmd);
    unsigned char s = 0;
    for (const char *p = body; *p; p++)
        s ^= (unsigned char)*p;
    sprintf(out, "$%s*%02X\n", body, s);
}

static void feed(const char *bytes) { size_t n = strlen(bytes); memcpy(rx + rxn, bytes, n); rxn += n; }
static void feed_cmd(unsigned seq, const char *cmd, const char *arg) { char f[80]; agent_frame(f, seq, cmd, arg); feed(f); }
static void clear_tx(void) { txn = 0; tx[0] = 0; }
static int count(const char *needle) { int n = 0; for (const char *p = tx; (p = strstr(p, needle)); p++) n++; return n; }

static bool refuse_time(const struct link_msg *m) { return strcmp(m->cmd, "TIME") != 0 || m->arg >= 1767225600u; }

int main(void)
{
    struct link_msg m;
    char want[80];

    /* 1. frames amid boot noise, a bad checksum, an old-format frame without seq, over-long junk */
    link_reset();
    feed("[    0.000] Linux version 5.10.4 $junk*ZZ\n");
    feed_cmd(0, "READY", NULL);
    feed("$1,SLEEP,5*00\n");                              /* bad checksum: no answer, no delivery */
    feed("$SLEEP,600*62\n");                              /* no seq: rejected */
    feed_cmd(1, "SLEEP", "600");
    feed("\r\n$THIS_IS_A_VERY_LONG_GARBAGE_FRAME_THAT_SHOULD_OVERFLOW_THE_BUFFER_XXXXXXXXXXXXXXXXXX*00\n");
    feed_cmd(2, "HALTED", NULL);
    const char *order[] = { "READY", "SLEEP", "HALTED" };
    int k = 0;
    while (link_poll(&m)) {
        CHECK(k < 3 && strcmp(m.cmd, order[k]) == 0, "frame %d is %s", k, m.cmd);
        if (k == 1) CHECK(m.has_arg && m.arg == 600 && strcmp(m.args, "600") == 0, "SLEEP arg");
        CHECK(m.seq == (unsigned)k, "seq %u", m.seq);
        k++;
    }
    CHECK(k == 3, "delivered %d frames", k);
    agent_frame(want, 0, "ACK", "0"); CHECK(strstr(tx, want) != NULL, "ACK for READY missing: %s", tx);
    agent_frame(want, 1, "ACK", "1"); CHECK(strstr(tx, want) != NULL, "ACK for SLEEP");
    agent_frame(want, 2, "ACK", "2"); CHECK(strstr(tx, want) != NULL, "ACK for HALTED");
    CHECK(count("ACK") == 3, "exactly 3 answers, got %d: %s", count("ACK"), tx);

    /* 2. a repeat (our ACK was lost) is answered again but not delivered twice */
    clear_tx();
    feed_cmd(2, "HALTED", NULL);
    CHECK(!link_poll(&m), "duplicate delivered");
    agent_frame(want, 2, "ACK", "2"); CHECK(strcmp(tx, want) == 0, "duplicate re-ACK: %s", tx);

    /* 3. the verdict refuses a TIME before 2026: NAK, not delivered; the repeat gets NAK again */
    link_set_verdict(refuse_time);
    clear_tx();
    feed_cmd(3, "TIME", "12345");
    CHECK(!link_poll(&m), "refused TIME delivered");
    agent_frame(want, 3, "NAK", "3"); CHECK(strcmp(tx, want) == 0, "NAK expected: %s", tx);
    clear_tx();
    feed_cmd(3, "TIME", "12345");
    CHECK(!link_poll(&m) && strcmp(tx, want) == 0, "repeat of a refused command must get NAK again");
    clear_tx();
    feed_cmd(4, "TIME", "1790000000");
    CHECK(link_poll(&m) && strcmp(m.cmd, "TIME") == 0 && m.arg == 1790000000u, "valid TIME");
    agent_frame(want, 4, "ACK", "4"); CHECK(strcmp(tx, want) == 0, "ACK for TIME");
    link_set_verdict(NULL);

    /* 4. our commands: sent on the first service call, resent every 300 ms, given up after 5 */
    link_reset();
    clear_tx();
    CHECK(link_send_cmd("WAKE", "leak,1790000000") && link_send_cmd("ENV", "655,213"), "queue");
    uint64_t t = 1000;
    CHECK(link_service(t) == NULL, "first send reports nothing");
    agent_frame(want, 0, "WAKE", "leak,1790000000"); CHECK(strcmp(tx, want) == 0, "WAKE frame: %s", tx);
    CHECK(link_service(t + 299) == NULL && count("WAKE") == 1, "no resend before the timeout");
    const struct link_result *r = NULL;
    for (int i = 1; i <= 5 && !r; i++)
        r = link_service(t + 300u * (unsigned)i);
    CHECK(count("WAKE") == 5, "WAKE sent 5 times, got %d", count("WAKE"));
    CHECK(r && r->end == LINK_TIMED_OUT && strcmp(r->cmd, "WAKE") == 0, "WAKE given up");

    /* 5. the next command follows with a new seq; a stale ACK (old seq) does not end it, the
     * right one does; then the queue is empty */
    clear_tx();
    CHECK(link_service(t + 2000) == NULL, "ENV sent");
    agent_frame(want, 1, "ENV", "655,213"); CHECK(strcmp(tx, want) == 0, "ENV frame with seq 1: %s", tx);
    feed_cmd(9, "ACK", "0");
    CHECK(!link_poll(&m), "an ACK is never delivered");
    CHECK(link_busy(), "stale ACK must not end ENV");
    feed_cmd(9, "ACK", "1");
    CHECK(!link_poll(&m), "ACK consumed");
    r = link_service(t + 2001);
    CHECK(r && r->end == LINK_ACKED && strcmp(r->cmd, "ENV") == 0, "ENV acknowledged");
    CHECK(!link_busy() && link_service(t + 5000) == NULL, "queue empty");

    /* 6. a NAK ends a command at once, without resends */
    clear_tx();
    link_send_cmd("SHUTDOWN", NULL);
    link_service(t + 6000);
    feed_cmd(10, "NAK", "2");
    link_poll(&m);
    r = link_service(t + 6001);
    CHECK(r && r->end == LINK_NAKED && strcmp(r->cmd, "SHUTDOWN") == 0, "SHUTDOWN refused");
    CHECK(link_service(t + 9000) == NULL && count("SHUTDOWN") == 1, "no resend after NAK");

    /* 7. READY from a restarted agent: seq 0 again is new, and our stale queue is dropped */
    link_send_cmd("WAKE", "rtc,1790000000");
    link_service(t + 10000);
    feed_cmd(0, "READY", NULL);
    CHECK(link_poll(&m) && strcmp(m.cmd, "READY") == 0, "READY after restart delivered");
    feed_cmd(0, "SLEEP", "60");                           /* same seq as READY: a repeat of it */
    CHECK(!link_poll(&m), "repeat of seq 0 not delivered");
    feed_cmd(1, "SLEEP", "60");
    CHECK(link_poll(&m) && strcmp(m.cmd, "SLEEP") == 0 && m.arg == 60, "next seq delivered");
    CHECK(!link_busy(), "queue dropped on READY");

    /* 8. queue limit: one in flight plus LINK_QUEUE waiting */
    link_reset();
    int ok = 0;
    for (int i = 0; i < LINK_QUEUE + 3; i++)
        ok += link_send_cmd("ENV", "1,1");
    CHECK(ok == LINK_QUEUE + 1, "queue holds %d, accepted %d", LINK_QUEUE + 1, ok);

    printf(fails ? "FAIL (%d)\n" : "link protocol: seq/ACK/NAK, duplicates, resends, give-up, stale answers, READY restart, noise\n", fails);
    return fails != 0;
}
