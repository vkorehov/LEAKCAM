/* Host test: the K230 agent's link code against the BL616's k230_link.c over a socket pair,
 * with frames lost on purpose. Both sides are the real sources: the agent through an include
 * with its main() renamed, the BL616 through a UART stub on the other socket end.
 *   make -C firmware/k230_agent test */
#define main agent_main
#include "../leakcam_agent.c"
#undef main

#include <sys/socket.h>
#include "k230_link.h"
#include "bflb_uart.h"

static int bl_fd;                      /* BL616 end of the socket pair */
static unsigned drop_bl_tx;            /* bit n set: drop the BL616's n-th frame from now on */
static unsigned drop_ag_tx;            /* same for frames from the agent (applied on BL616 receive) */
static unsigned bl_tx_n, ag_tx_n;
static char out_frame[96];
static size_t out_len;
static char in_frame[96];
static size_t in_len, in_pos;
static bool in_ready;

struct bflb_device_s *bflb_device_get_by_name(const char *n) { (void)n; static struct bflb_device_s d; return &d; }

/* whole frames are passed or dropped, so a loss never leaves half a line behind */
int bflb_uart_putchar(struct bflb_device_s *d, int c)
{
    (void)d;
    out_frame[out_len++] = (char)c;
    if (c == '\n' || out_len == sizeof(out_frame)) {
        if (!(drop_bl_tx & (1u << (bl_tx_n & 31))))
            if (write(bl_fd, out_frame, out_len) != (ssize_t)out_len) perror("write");
        bl_tx_n++;
        out_len = 0;
    }
    return 0;
}

int bflb_uart_getchar(struct bflb_device_s *d)
{
    (void)d;
    for (;;) {
        if (in_ready) {                 /* serve a kept line byte by byte */
            if (in_pos < in_len)
                return (unsigned char)in_frame[in_pos++];
            in_ready = false;
            in_len = 0;
        }
        char c;
        if (read(bl_fd, &c, 1) != 1)
            return -1;                  /* nothing buffered: the non-blocking UART is empty */
        in_frame[in_len++] = c;
        if (c == '\n' || in_len == sizeof(in_frame)) {
            bool drop = drop_ag_tx & (1u << (ag_tx_n & 31));
            ag_tx_n++;
            if (drop)
                in_len = 0;
            else {
                in_ready = true;
                in_pos = 0;
            }
        }
    }
}

/* ---- the BL616 side, run in its own thread like the power manager's session loop ---- */
static volatile bool bl_run = true;
static struct link_msg bl_got[16];
static volatile int bl_ngot;
static const struct link_result *volatile bl_last;
static struct link_result bl_res[16];
static volatile int bl_nres;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char bl_queue_cmd[16], bl_queue_arg[40];
static volatile bool bl_queue;

static bool bl_verdict(const struct link_msg *m) { return strcmp(m->cmd, "TIME") != 0 || m->arg >= 1767225600u; }

static void *bl616(void *arg)
{
    (void)arg;
    link_reset();
    link_set_verdict(bl_verdict);
    while (bl_run) {
        pthread_mutex_lock(&mu);
        if (bl_queue) { link_send_cmd(bl_queue_cmd, bl_queue_arg[0] ? bl_queue_arg : NULL); bl_queue = false; }
        const struct link_result *r = link_service(now_ms());
        if (r && bl_nres < 16) bl_res[bl_nres++] = *r;
        struct link_msg m;
        while (link_poll(&m))
            if (bl_ngot < 16) bl_got[bl_ngot++] = m;
        pthread_mutex_unlock(&mu);
        usleep(2000);
    }
    return NULL;
}

static void bl_send(const char *cmd, const char *arg)
{
    pthread_mutex_lock(&mu);
    snprintf(bl_queue_cmd, sizeof(bl_queue_cmd), "%s", cmd);
    snprintf(bl_queue_arg, sizeof(bl_queue_arg), "%s", arg ? arg : "");
    bl_queue = true;
    pthread_mutex_unlock(&mu);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int got(const char *cmd)
{
    int n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < bl_ngot; i++) n += strcmp(bl_got[i].cmd, cmd) == 0;
    pthread_mutex_unlock(&mu);
    return n;
}

static const struct link_result *wait_result(int idx, int ms)
{
    uint64_t end = now_ms() + (uint64_t)ms;
    while (now_ms() < end) {
        pthread_mutex_lock(&mu);
        int n = bl_nres;
        pthread_mutex_unlock(&mu);
        if (n > idx) return &bl_res[idx];
        usleep(5000);
    }
    return NULL;
}

int main(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { perror("socketpair"); return 1; }
    uart_fd = sv[0];
    bl_fd = sv[1];
    fcntl(bl_fd, F_SETFL, O_NONBLOCK);
    pthread_t th;
    pthread_create(&th, NULL, bl616, NULL);
    usleep(20000);

    /* 1. READY with the BL616's first two answers lost: resent, acknowledged, delivered once */
    drop_bl_tx = 0x3;
    enum link_end e = link_cmd("READY", NULL);
    CHECK(e == LINK_ACKED, "READY %s", link_end_name(e));
    CHECK(got("READY") == 1, "READY delivered %d times", got("READY"));
    drop_bl_tx = 0;

    /* 2. WAKE with its first send lost and the agent's first ACK lost: the agent gets it once */
    bl_tx_n = 0; ag_tx_n = 0;
    drop_bl_tx = 0x1;               /* the first WAKE frame */
    drop_ag_tx = 0x1;               /* the agent's ACK to the second send */
    bl_send("WAKE", "leak,1790000000");
    struct frame f;
    int wakes = 0;
    uint64_t end = now_ms() + 2500;
    while (now_ms() < end)
        if (next_cmd(&f, 100) && strcmp(f.cmd, "WAKE") == 0) {
            wakes++;
            CHECK(strcmp(f.arg, "leak,1790000000") == 0, "WAKE arg %s", f.arg);
        }
    const struct link_result *r = wait_result(0, 500);
    CHECK(wakes == 1, "WAKE delivered %d times", wakes);
    CHECK(r && r->end == LINK_ACKED && strcmp(r->cmd, "WAKE") == 0, "WAKE %s", r ? (r->end == LINK_ACKED ? "acked" : "not acked") : "no result");
    drop_bl_tx = drop_ag_tx = 0;

    /* 3. ENV out of range is refused by the agent: the BL616 sees NAK and does not resend */
    bl_send("ENV", "1500,213");
    end = now_ms() + 600;
    while (now_ms() < end)
        CHECK(!next_cmd(&f, 50) || strcmp(f.cmd, "ENV") != 0, "refused ENV delivered");
    r = wait_result(1, 500);
    CHECK(r && r->end == LINK_NAKED && strcmp(r->cmd, "ENV") == 0, "ENV must be refused");

    /* 4. TIME before 2026 refused by the BL616 verdict, a real time accepted */
    e = link_cmd("TIME", "12345");
    CHECK(e == LINK_NAKED, "old TIME %s", link_end_name(e));
    e = link_cmd("TIME", "1790000000");
    CHECK(e == LINK_ACKED && got("TIME") == 1, "TIME %s, delivered %d", link_end_name(e), got("TIME"));

    /* 5. every BL616 answer lost: the agent gives up after LINK_TRIES sends, about 1.5 s */
    drop_bl_tx = ~0u;
    uint64_t t0 = now_ms();
    e = link_cmd("SLEEP", "600");
    uint64_t dt = now_ms() - t0;
    CHECK(e == LINK_TIMED_OUT, "SLEEP %s", link_end_name(e));
    CHECK(dt >= LINK_ACK_TIMEOUT_MS * LINK_TRIES - 50 && dt < LINK_ACK_TIMEOUT_MS * LINK_TRIES + 500, "gave up after %llu ms", (unsigned long long)dt);
    CHECK(got("SLEEP") == 1, "SLEEP delivered %d times despite 5 sends", got("SLEEP"));
    drop_bl_tx = 0;

    bl_run = false;
    pthread_join(th, NULL);
    printf(fails ? "FAIL (%d)\n" : "link end to end: agent vs BL616 over a lossy line, resends, duplicates, NAK both ways, give-up\n", fails);
    return fails != 0;
}
