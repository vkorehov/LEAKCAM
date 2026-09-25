/*
 * leakcam_agent: K230 (Linux, little core) side of the BL616 power protocol.
 *
 *  - toggles K230 GPIO2 (net K230_ALIVE -> BL616 IO01) as a heartbeat, every 500 ms;
 *  - talks to the BL616 over UART1 (GPIO40 TXD / GPIO41 RXD), protocol in bl616_pwrmgr/k230_link.h;
 *  - runs the capture hook for the wake reason, then asks to be powered off; the BL616's last
 *    humidity sample (ENV frame) is passed to the hook as LEAKCAM_RH / LEAKCAM_T;
 *  - sets the system clock from the time in the BL616's WAKE frame (the K230 has no running
 *    clock after power-up), and sends TIME back when the K230 itself is NTP-synchronised;
 *  - every frame both ways carries a sequence number and is answered ACK/NAK, with resends
 *    (link section below, same rules as bl616_pwrmgr/k230_link.h);
 *  - before power is cut: sync, remount / read-only, sync, send HALTED.
 *
 * Needs in the device tree: uart1 enabled on IO40/IO41, and IO2 muxed as GPIO (its reset
 * function is JTAG_TCK). Build: see Makefile (k230_sdk RISC-V Linux toolchain).
 *
 *   leakcam_agent [-u /dev/ttyS1] [-c /dev/gpiochip0] [-l 2] [-x /etc/leakcam/on-wake]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/timex.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "k230_link.h"

#define HEARTBEAT_HALF_PERIOD_MS 500
#define READY_RETRY_MS           1000
#define READY_TRIES              15
#define DEFAULT_SLEEP_S          (6 * 3600)

static const char *uart_dev = "/dev/ttyS1";
static const char *chip_dev = "/dev/gpiochip0";
static unsigned alive_line = 2;               /* K230 IO2; confirm offset with `gpioinfo` */
static const char *hook = "/etc/leakcam/on-wake";

static int uart_fd = -1;
static int alive_fd = -1;
static volatile sig_atomic_t heartbeat_run = 1;
static volatile sig_atomic_t shutdown_req;
static pid_t hook_pid;

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "leakcam_agent: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

/* ---------------- GPIO heartbeat (GPIO character device, uAPI v1) ---------------- */

static int alive_open(void)
{
    int chip = open(chip_dev, O_RDONLY | O_CLOEXEC);
    if (chip < 0) {
        logmsg("open %s: %s", chip_dev, strerror(errno));
        return -1;
    }
    struct gpiohandle_request req = { 0 };
    req.lineoffsets[0] = alive_line;
    req.lines = 1;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    strncpy(req.consumer_label, "leakcam-alive", sizeof(req.consumer_label) - 1);
    int rc = ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req);
    close(chip);
    if (rc < 0) {
        logmsg("request line %u on %s: %s (is IO2 still muxed as JTAG_TCK?)", alive_line, chip_dev, strerror(errno));
        return -1;
    }
    alive_fd = req.fd;
    return 0;
}

static void alive_set(int v)
{
    struct gpiohandle_data d = { .values = { (uint8_t)v } };
    if (ioctl(alive_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d) < 0)
        logmsg("set alive: %s", strerror(errno));
}

static void *heartbeat_thread(void *arg)
{
    (void)arg;
    int v = 0;
    struct timespec half = { 0, HEARTBEAT_HALF_PERIOD_MS * 1000000L };
    while (heartbeat_run) {
        v ^= 1;
        alive_set(v);
        nanosleep(&half, NULL);
    }
    return NULL;
}

/* ---------------- UART link ---------------- */

static int uart_open(void)
{
    uart_fd = open(uart_dev, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (uart_fd < 0) {
        logmsg("open %s: %s", uart_dev, strerror(errno));
        return -1;
    }
    struct termios t;
    if (tcgetattr(uart_fd, &t) < 0)
        return -1;
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcflush(uart_fd, TCIOFLUSH);              /* drop anything the BL616 printed before we opened */
    return tcsetattr(uart_fd, TCSANOW, &t);
}

/* ---------------- link: $<seq>,<CMD>[,args]*XX, every command answered ACK/NAK ---------------- */

/* LINK_ACK_TIMEOUT_MS, LINK_TRIES and enum link_end come from the protocol header shared
 * with the BL616, bl616_pwrmgr/k230_link.h */
#define LINK_INBOX          4

struct frame {
    char cmd[16];
    char arg[32];
    unsigned seq;
};

static unsigned tx_next;                /* our next seq, 0..255 */
static int rx_last = -1;                /* last seq accepted from the BL616, and our answer */
static bool rx_last_ok;
static struct frame inbox[LINK_INBOX];  /* commands that arrived while we waited for an answer */
static unsigned in_head, in_count;

static void send_frame(unsigned seq, const char *cmd, const char *arg)
{
    char body[64], frame[80];
    if (arg && *arg)
        snprintf(body, sizeof(body), "%u,%s,%s", seq, cmd, arg);
    else
        snprintf(body, sizeof(body), "%u,%s", seq, cmd);
    uint8_t sum = 0;
    for (const char *p = body; *p; p++)
        sum ^= (uint8_t)*p;
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, sum);
    if (write(uart_fd, frame, n) != n)
        logmsg("uart write: %s", strerror(errno));
    tcdrain(uart_fd);
}

static void send_answer(unsigned seq, bool ok)
{
    char s[4];
    snprintf(s, sizeof(s), "%u", seq);
    send_frame(seq, ok ? "ACK" : "NAK", s);
}

/* frames longer than the caller's buffer are rejected, never truncated into a valid-looking command */
static bool copy_field(char *dst, size_t dstlen, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstlen)
        return false;
    memcpy(dst, src, n + 1);
    return true;
}

/* returns true with f filled when a valid frame arrives within timeout_ms */
static bool read_frame(struct frame *f, int timeout_ms)
{
    static char line[64];
    static size_t len;
    static bool in_frame;
    uint64_t end = now_ms() + (uint64_t)timeout_ms;

    while (1) {
        int left = (int)(end - now_ms());
        if (left < 0)
            left = 0;                   /* timeout 0 = one non-blocking pass over what is buffered */
        struct pollfd p = { .fd = uart_fd, .events = POLLIN };
        int rc = poll(&p, 1, left);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc <= 0)
            return false;
        char c;
        while (read(uart_fd, &c, 1) == 1) {
            if (c == '$') { in_frame = true; len = 0; continue; }
            if (!in_frame || c == '\r') continue;
            if (c != '\n') {
                if (len < sizeof(line) - 1) line[len++] = c; else in_frame = false;
                continue;
            }
            in_frame = false;
            line[len] = 0;
            char *star = strchr(line, '*');
            if (!star) continue;
            uint8_t sum = 0;
            for (char *q = line; q < star; q++) sum ^= (uint8_t)*q;
            if (strtoul(star + 1, NULL, 16) != sum) continue;
            *star = 0;
            char *end_seq;
            unsigned long seq = strtoul(line, &end_seq, 10);
            if (end_seq == line || *end_seq != ',' || seq > 255) continue;
            char *cmd = end_seq + 1, *comma = strchr(cmd, ',');
            if (comma) *comma = 0;
            if (*cmd && copy_field(f->cmd, sizeof(f->cmd), cmd) &&
                copy_field(f->arg, sizeof(f->arg), comma ? comma + 1 : "")) {
                f->seq = (unsigned)seq;
                return true;
            }
        }
    }
}

/* AHT20 range: 0-100 %RH, -40..+85 C; anything else is refused as a corrupted value */
static bool env_valid(const char *arg, long *rh, long *t)
{
    char *comma = strchr(arg, ',');
    *rh = strtol(arg, NULL, 10);
    *t = comma ? strtol(comma + 1, NULL, 10) : -9999;
    return comma && *rh >= 0 && *rh <= 1000 && *t >= -400 && *t <= 850;
}

/* answer a command from the BL616 (once per seq; a repeat gets the same answer again) and
 * tell whether it is new and accepted, i.e. should be acted on */
static bool accept_cmd(const struct frame *f)
{
    if (rx_last == (int)f->seq) {
        send_answer(f->seq, rx_last_ok);
        return false;
    }
    long rh, t;
    rx_last = (int)f->seq;
    rx_last_ok = strcmp(f->cmd, "ENV") != 0 || env_valid(f->arg, &rh, &t);
    send_answer(f->seq, rx_last_ok);
    return rx_last_ok;
}

static void inbox_put(const struct frame *f)
{
    if (in_count == LINK_INBOX) {
        logmsg("link inbox full, %s dropped", f->cmd);
        return;
    }
    inbox[(in_head + in_count++) % LINK_INBOX] = *f;
}

/* send a command and wait for its answer, resending up to LINK_TRIES times; commands that
 * arrive meanwhile are answered and kept for next_cmd() */
static enum link_end link_cmd(const char *cmd, const char *arg)
{
    unsigned seq = tx_next;
    tx_next = (tx_next + 1) & 0xff;
    for (int i = 0; i < LINK_TRIES; i++) {
        send_frame(seq, cmd, arg);
        uint64_t until = now_ms() + LINK_ACK_TIMEOUT_MS;
        struct frame f;
        while (now_ms() < until && read_frame(&f, (int)(until - now_ms()))) {
            bool ack = strcmp(f.cmd, "ACK") == 0;
            if (ack || strcmp(f.cmd, "NAK") == 0) {
                if (strtoul(f.arg, NULL, 10) == seq)
                    return ack ? LINK_ACKED : LINK_NAKED;
                continue;               /* a stale answer */
            }
            if (accept_cmd(&f))
                inbox_put(&f);
        }
    }
    return LINK_TIMED_OUT;
}

/* next new, accepted command from the BL616 within timeout_ms (0 = only what is buffered) */
static bool next_cmd(struct frame *f, int timeout_ms)
{
    if (in_count) {
        *f = inbox[in_head];
        in_head = (in_head + 1) % LINK_INBOX;
        in_count--;
        return true;
    }
    uint64_t until = now_ms() + (uint64_t)timeout_ms;
    for (;;) {
        int left = (int)(until - now_ms());
        if (!read_frame(f, left > 0 ? left : 0))
            return false;
        if (strcmp(f->cmd, "ACK") == 0 || strcmp(f->cmd, "NAK") == 0)
            continue;                   /* an answer nobody waits for any more */
        if (accept_cmd(f))
            return true;
    }
}

static const char *link_end_name(enum link_end e)
{
    return e == LINK_ACKED ? "acknowledged" : e == LINK_NAKED ? "refused" : "not answered";
}

/* ---------------- clock ---------------- */

/* earliest plausible time, 2026-01-01T00:00:00Z: anything before is a default, not real time */
#define EPOCH_MIN 1767225600L

/* true when the kernel clock is disciplined by NTP (chrony / systemd-timesyncd clear STA_UNSYNC) */
static bool ntp_synced(void)
{
    struct timex t;
    memset(&t, 0, sizeof(t));
    int s = adjtimex(&t);
    return s >= 0 && s != TIME_ERROR && !(t.status & STA_UNSYNC);
}

/* the time in WAKE,<reason>,<unix s>: the BL616's crystal clock beats our power-up default */
static void clock_from_bl616(const char *arg)
{
    char *end;
    long v = strtol(arg, &end, 10);
    if (*end || v == 0) {
        logmsg("BL616 has no valid time yet");
        return;
    }
    if (v < EPOCH_MIN) {
        logmsg("time %s from BL616 ignored", arg);
        return;
    }
    if (ntp_synced()) {
        logmsg("time from BL616 ignored, NTP already synchronised");
        return;
    }
    struct timespec ts = { .tv_sec = v, .tv_nsec = 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) < 0)
        logmsg("clock_settime: %s", strerror(errno));
    else
        logmsg("clock set from BL616: %ld", v);
}

/* ---------------- shutdown ---------------- */

static void make_safe_for_power_cut(void)
{
    if (hook_pid > 0) {
        kill(hook_pid, SIGTERM);
        for (int i = 0; i < 50 && waitpid(hook_pid, NULL, WNOHANG) == 0; i++)
            usleep(100000);
        kill(hook_pid, SIGKILL);
        waitpid(hook_pid, NULL, 0);
        hook_pid = 0;
    }
    sync();
    /* read-only root: a power cut can then no longer interrupt a UBIFS commit on the SPI NAND */
    if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
        logmsg("remount / ro: %s (continuing, data is synced)", strerror(errno));
    sync();
}

/* tenths to text, sign-correct for -0.5 */
static void fmt_x10(char *out, size_t n, long v)
{
    long a = v < 0 ? -v : v;
    snprintf(out, n, "%s%ld.%ld", v < 0 ? "-" : "", a / 10, a % 10);
}

static void on_signal(int s)
{
    (void)s;
    shutdown_req = 1;
}

/* ---------------- main ---------------- */

static int run_hook(const char *reason, unsigned *sleep_s)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;
    hook_pid = fork();
    if (hook_pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl(hook, hook, reason, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);

    /* the hook may print "sleep=<seconds>" to choose the next wake-up */
    char buf[128];
    ssize_t n;
    size_t used = 0;
    char out[512] = "";
    while (used < sizeof(out) - 1) {
        struct pollfd p = { .fd = pipefd[0], .events = POLLIN };
        int rc = poll(&p, 1, 200);
        if (shutdown_req)
            break;
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc == 0) {
            /* keep answering BL616 frames while the hook runs: it may send SHUTDOWN */
            struct frame f;
            while (next_cmd(&f, 0))
                if (strcmp(f.cmd, "SHUTDOWN") == 0)
                    shutdown_req = 1;
            continue;
        }
        n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        size_t k = (size_t)n < sizeof(out) - 1 - used ? (size_t)n : sizeof(out) - 1 - used;
        memcpy(out + used, buf, k);
        used += k;
        out[used] = 0;
    }
    close(pipefd[0]);
    int status = 0;
    if (!shutdown_req)
        waitpid(hook_pid, &status, 0);
    char *s = strstr(out, "sleep=");
    if (s)
        *sleep_s = (unsigned)strtoul(s + 6, NULL, 10);
    if (!shutdown_req)
        hook_pid = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "u:c:l:x:")) != -1) {
        switch (opt) {
            case 'u': uart_dev = optarg; break;
            case 'c': chip_dev = optarg; break;
            case 'l': alive_line = (unsigned)strtoul(optarg, NULL, 0); break;
            case 'x': hook = optarg; break;
            default:
                fprintf(stderr, "usage: %s [-u tty] [-c gpiochip] [-l line] [-x hook]\n", argv[0]);
                return 2;
        }
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    if (alive_open() < 0 || uart_open() < 0)
        return 1;
    pthread_t hb;
    pthread_create(&hb, NULL, heartbeat_thread, NULL);

    char reason[32] = "cold";
    bool got_wake = false;
    struct frame f;
    for (int i = 0; i < READY_TRIES && !got_wake && !shutdown_req; i++) {
        enum link_end e = link_cmd("READY", NULL);
        if (e != LINK_ACKED) {
            logmsg("READY %s", link_end_name(e));
            continue;
        }
        uint64_t until = now_ms() + READY_RETRY_MS;
        while (!got_wake && now_ms() < until && next_cmd(&f, (int)(until - now_ms()))) {
            if (strcmp(f.cmd, "WAKE") == 0) {
                char *comma = strchr(f.arg, ',');
                if (comma)
                    *comma = 0;
                snprintf(reason, sizeof(reason), "%s", f.arg);
                if (comma)
                    clock_from_bl616(comma + 1);
                got_wake = true;
            } else if (strcmp(f.cmd, "SHUTDOWN") == 0) {
                shutdown_req = 1;
            }
        }
    }
    if (!got_wake)
        logmsg("no WAKE from BL616, assuming cold start");
    logmsg("wake reason: %s", reason);

    /* after WAKE, within a moment and only with a fresh AHT20 sample: ENV,<rh_x10>,<t_x10>
     * (out-of-range values were already refused with NAK) */
    uint64_t follow_end = now_ms() + 300;
    while (got_wake && now_ms() < follow_end && next_cmd(&f, (int)(follow_end - now_ms()))) {
        if (strcmp(f.cmd, "SHUTDOWN") == 0) {
            shutdown_req = 1;
            continue;
        }
        long rh, t;
        if (strcmp(f.cmd, "ENV") != 0 || !env_valid(f.arg, &rh, &t))
            continue;
        char v[24];
        fmt_x10(v, sizeof(v), rh);
        setenv("LEAKCAM_RH", v, 1);
        fmt_x10(v, sizeof(v), t);
        setenv("LEAKCAM_T", v, 1);
        logmsg("humidity %s %%RH, %s C", getenv("LEAKCAM_RH"), v);
    }

    unsigned sleep_s = DEFAULT_SLEEP_S;
    if (!shutdown_req) {
        int rc = run_hook(reason, &sleep_s);
        logmsg("hook exited %d, next wake in %u s", rc, sleep_s);
    }

    /* real time learned during the session (NTP over Wi-Fi): hand it to the BL616, whose clock
     * then survives until the next battery change and corrects its crystal drift */
    if (!shutdown_req && ntp_synced()) {
        char t[16];
        snprintf(t, sizeof(t), "%ld", (long)time(NULL));
        enum link_end e = link_cmd("TIME", t);
        if (e != LINK_ACKED)
            logmsg("TIME %s", link_end_name(e));
    }

    if (!shutdown_req) {
        char s[16];
        snprintf(s, sizeof(s), "%u", sleep_s);
        /* the BL616 cuts power HALT_TIMEOUT_MS after SLEEP (or after SHUTDOWN) regardless */
        enum link_end e = link_cmd("SLEEP", s);
        if (e != LINK_ACKED)
            logmsg("SLEEP %s", link_end_name(e));
    }
    make_safe_for_power_cut();
    enum link_end e = link_cmd("HALTED", NULL);
    if (e != LINK_ACKED)
        logmsg("HALTED %s", link_end_name(e));

    /* keep the heartbeat running: stopping it now would only make the BL616 log a spurious
     * "heartbeat lost" if its power cut is delayed */
    for (;;)
        pause();
}
