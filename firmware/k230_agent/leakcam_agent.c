/*
 * leakcam_agent: K230 (RT-Smart) side of the BL616 power protocol, started at boot through
 * RTT_AUTO_EXEC_CMD.
 *
 *  - toggles K230 GPIO2 (net K230_ALIVE -> BL616 IO01) as a heartbeat, every 500 ms: the K230
 *    stays powered while it runs, and stopping it is how the session ends (the BL616 cuts the
 *    power 2 s after the last edge; there is no shutdown command);
 *  - talks to the BL616 over UART1 (GPIO40 TXD / GPIO41 RXD, /dev/uart1), protocol in
 *    bl616/k230_link.h;
 *  - runs the capture hook for the wake reason (killed after HOOK_TIMEOUT_S: nothing else ends
 *    a session that hangs), sends SLEEP,<s> for the next wake, then stops the heartbeat; the BL616's
 *    sensor values (in the WAKE frame) are passed to the hook as LEAKCAM_RH / LEAKCAM_T /
 *    LEAKCAM_PROBE_MV, the battery (our ADC_1, answered to WAKE) as LEAKCAM_BAT_MV; a line "wifi"
 *    from the hook sends WIFI, the only way the BL616's Wi-Fi is started;
 *  - sets the system clock from the time in the BL616's WAKE frame (the K230 has no running
 *    clock after power-up), and sends TIME back when NTP has set the clock during the session;
 *  - every frame both ways carries a sequence number and is answered ACK/NAK, with resends
 *    (link section below, same rules as bl616/k230_link.h);
 *  - before the heartbeat stops the hook has exited. UFFS (/sdcard) is log-structured and
 *    survives a power cut; RT-Smart has no sync() or remount, so programs fsync what they write.
 *
 * The hook is a program (RT-Smart has no shell): <hook> <reasons>, the BL616's wake reasons joined
 * by '+' (cold, leak, rtc, usb, humid: "rtc+leak+humid"), with the BL616's
 * AHT20 sample in LEAKCAM_RH / LEAKCAM_T, the probe voltage in
 * LEAKCAM_PROBE_MV and the battery in LEAKCAM_BAT_MV. It may print
 * "sleep=<seconds>" for the next
 * scheduled wake-up, and a line "wifi" when it needs the network; the answer, "wifi=ok" or
 * "wifi=fail,<code>", comes back on its stdin.
 *
 * Pins (IO2 as GPIO, IO40/IO41 as UART1) are set by the board pinmux (k230_board/pins.py).
 * Built into the image by the SDK (k230_board/install.sh); the host test is test/.
 *
 *   leakcam_agent [-u 1] [-l 2] [-x /sdcard/app/leakcam_wake]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "k230_link.h"

#define HEARTBEAT_HALF_PERIOD_MS 500
#define READY_RETRY_MS           1000
#define READY_TRIES              15
#define DEFAULT_SLEEP_S          (6 * 3600)
#define HOOK_TIMEOUT_S           600u    /* a hook still running then is killed */

static int uart_id = 1;                       /* /dev/uart1 on IO40/IO41 */
static int alive_pin = 2;                     /* K230 IO2 */
static const char *hook = "/sdcard/app/leakcam_wake";

static int uart_fd = -1;
static int gpio_fd = -1;
static volatile sig_atomic_t heartbeat_run = 1;

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

/* ---------------- GPIO heartbeat ---------------- */

/* the RT-Smart GPIO device: one ioctl sets a pin's mode, then the file offset is the pin and
 * one byte written is its level (kernel ABI of bsp/maix3 drv_gpio, 'G' 0 = set mode, 0 = output) */
struct gpio_cfg {
    uint16_t pin;
    uint16_t value;
};
#define GPIO_IOCTL_SET_MODE _IOW('G', 0, struct gpio_cfg *)
#define GPIO_MODE_OUTPUT    0

static void alive_set(int v)
{
    uint8_t b = (uint8_t)v;
    if (lseek(gpio_fd, alive_pin, SEEK_SET) != alive_pin || write(gpio_fd, &b, 1) != 1)
        logmsg("set GPIO%d: %s", alive_pin, strerror(errno));
}

static int alive_open(void)
{
    struct gpio_cfg c = { .pin = (uint16_t)alive_pin, .value = GPIO_MODE_OUTPUT };
    gpio_fd = open("/dev/gpio", O_RDWR);
    if (gpio_fd < 0 || ioctl(gpio_fd, GPIO_IOCTL_SET_MODE, &c) != 0) {
        logmsg("GPIO%d as output: %s", alive_pin, strerror(errno));
        return -1;
    }
    alive_set(0);
    return 0;
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

/* /dev/uart<id> comes up at the kernel's default 115200 8N1 (RT_SERIAL_CONFIG_DEFAULT) */
static int uart_open(void)
{
    char dev[16];
    snprintf(dev, sizeof(dev), "/dev/uart%d", uart_id);
    uart_fd = open(dev, O_RDWR | O_NONBLOCK);
    if (uart_fd < 0) {
        logmsg("open %s: %s", dev, strerror(errno));
        return -1;
    }
    char junk[64];                            /* drop anything the BL616 printed before we opened */
    while (read(uart_fd, junk, sizeof(junk)) > 0) {
    }
    return 0;
}

/* ---------------- link: $<seq>,<CMD>[,args]*XX, every command answered ACK/NAK ---------------- */

/* LINK_ACK_TIMEOUT_MS, LINK_TRIES and enum link_end come from the protocol header shared
 * with the BL616, bl616/k230_link.h */
#define LINK_INBOX          4

struct frame {
    char cmd[16];
    char arg[56];                       /* WAKE with every reason and sensor: ~46 */
    unsigned seq;
};

static unsigned tx_next;                /* our next seq, 0..255 */
static int rx_last = -1;                /* last seq accepted from the BL616 */
static unsigned nak_code;               /* code of the last NAK link_cmd() got */
static struct frame inbox[LINK_INBOX];  /* commands that arrived while we waited for an answer */
static unsigned in_head, in_count;

static void send_frame(unsigned seq, const char *cmd, const char *arg)
{
    char body[64], frame[80];
    if (arg && *arg)
        snprintf(body, sizeof(body), "%u,%s,%s", seq, cmd, arg);
    else
        snprintf(body, sizeof(body), "%u,%s", seq, cmd);
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, link_crc8(body, body + strlen(body)));
    if (write(uart_fd, frame, n) != n)
        logmsg("uart write: %s", strerror(errno));
}

/* ACK,<seq>[,<answer>]: only WAKE's ACK carries an answer, our battery reading */
static void send_ack(unsigned seq, const char *answer)
{
    char s[24];
    if (answer)
        snprintf(s, sizeof(s), "%u,%s", seq, answer);
    else
        snprintf(s, sizeof(s), "%u", seq);
    send_frame(seq, "ACK", s);
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
            if (strtoul(star + 1, NULL, 16) != link_crc8(line, star)) continue;
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

/* tenths to text, sign-correct for -0.5 */
static void fmt_x10(char *out, size_t n, long v)
{
    long a = v < 0 ? -v : v;
    snprintf(out, n, "%s%ld.%ld", v < 0 ? "-" : "", a / 10, a % 10);
}

/* ---------------- battery: ADC_1 (ball A6) = VBAT / 3 ---------------- */

/* R59 200 k over R61 100 k, switched on with our 3V3 (Q2 -> Q1), so only readable while we run;
 * C92 100 nF holds the node, one conversion is enough. The RT-Smart ADC device: ioctl 0 enables
 * a channel, then a 4-byte read at file offset <channel> is one 12-bit conversion against the
 * 1.8 V ADC reference (kernel ABI of bsp/maix3 drv_adc and the RT-Thread ADC framework) */
#define BAT_ADC_CHANNEL  1
#define BAT_DIVIDER      3
#define ADC_IOCTL_ENABLE 0

static int bat_mv = -1;                 /* our reading this session, -1 = none */

static int battery_read_mv(void)
{
    uint32_t raw;
    int fd = open("/dev/adc", O_RDWR);
    bool ok = fd >= 0 && ioctl(fd, ADC_IOCTL_ENABLE, BAT_ADC_CHANNEL) == 0 &&
              lseek(fd, BAT_ADC_CHANNEL, SEEK_SET) == BAT_ADC_CHANNEL &&
              read(fd, &raw, sizeof(raw)) == (ssize_t)sizeof(raw) && raw <= 4095;
    if (fd >= 0)
        close(fd);
    if (!ok) {
        logmsg("battery: ADC channel %d: %s", BAT_ADC_CHANNEL, strerror(errno));
        return -1;
    }
    return (int)((raw * 1800u * BAT_DIVIDER + 4095u / 2) / 4095u);
}

/* the sensor part of WAKE, <rh_x10>,<t_x10>,<probe_mv>,<bat_mv>, for the hook as LEAKCAM_RH and
 * LEAKCAM_T (%RH, C), LEAKCAM_PROBE_MV and LEAKCAM_BAT_MV: our own battery reading, or the
 * BL616's copy of the previous session's if our ADC failed */
static void sensors_from_bl616(const char *arg)
{
    long rh = 0, t = 0, mv = 0, bat = 0;
    sscanf(arg, "%ld,%ld,%ld,%ld", &rh, &t, &mv, &bat);
    char v[24];
    fmt_x10(v, sizeof(v), rh);
    setenv("LEAKCAM_RH", v, 1);
    fmt_x10(v, sizeof(v), t);
    setenv("LEAKCAM_T", v, 1);
    snprintf(v, sizeof(v), "%ld", mv);
    setenv("LEAKCAM_PROBE_MV", v, 1);
    snprintf(v, sizeof(v), "%ld", bat_mv >= 0 ? (long)bat_mv : bat);
    setenv("LEAKCAM_BAT_MV", v, 1);
    logmsg("sensors: %s %%RH, %s C, probe %s mV, battery %s mV", getenv("LEAKCAM_RH"),
           getenv("LEAKCAM_T"), getenv("LEAKCAM_PROBE_MV"), getenv("LEAKCAM_BAT_MV"));
}

/* acknowledge a command from the BL616 (a repeat, whose ACK was lost, is acknowledged again)
 * and tell whether it is new, i.e. should be acted on. Nothing the BL616 sends is refused */
static bool accept_cmd(const struct frame *f)
{
    char bat[12];
    snprintf(bat, sizeof(bat), "%d", bat_mv);
    send_ack(f->seq, strcmp(f->cmd, "WAKE") == 0 ? bat : NULL);
    if (rx_last == (int)f->seq)
        return false;
    rx_last = (int)f->seq;
    return true;
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
                if (strtoul(f.arg, NULL, 10) == seq) {
                    const char *code = strchr(f.arg, ',');   /* NAK,<seq>,<code> */
                    nak_code = ack ? 0 : code ? (unsigned)strtoul(code + 1, NULL, 10) : LINK_ERR_REFUSED;
                    return ack ? LINK_ACKED : LINK_NAKED;
                }
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

/* NTP (netutils, over Wi-Fi) steps the clock when it syncs. Seen from here: the offset between
 * the real-time and the monotonic clock moves after the last time we set it ourselves. */
static int64_t clock_offset_s(void)
{
    struct timespec r, m;
    clock_gettime(CLOCK_REALTIME, &r);
    clock_gettime(CLOCK_MONOTONIC, &m);
    return (int64_t)r.tv_sec - (int64_t)m.tv_sec;
}

static int64_t own_offset;                    /* offset after our last clock_settime(), or at start */

static bool ntp_synced(void)
{
    int64_t d = clock_offset_s() - own_offset;
    return time(NULL) >= EPOCH_MIN && (d > 2 || d < -2);
}

/* the time in WAKE,<reason>,<unix s>,...: the BL616's crystal clock beats our power-up default */
static void clock_from_bl616(const char *arg)
{
    char *end;
    long v = strtol(arg, &end, 10);
    if ((*end && *end != ',') || v == 0) {
        logmsg("BL616 has no valid time yet");
        return;
    }
    if (v < EPOCH_MIN) {
        logmsg("time %s from BL616 ignored", arg);
        return;
    }
    struct timespec ts = { .tv_sec = v, .tv_nsec = 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
        logmsg("clock_settime: %s", strerror(errno));
    } else {
        own_offset = clock_offset_s();
        logmsg("clock set from BL616: %ld", v);
    }
}

/* ---------------- Wi-Fi ---------------- */

/* The BL616 starts its SDIO device only on WIFI, and nothing probes MMC0 at boot: /dev/bl616
 * (k230_board bl616_nethub driver) probes it and returns once wlan0 exists */
#define BL616_IOCTL_RESCAN 0x4c01            /* bl616_nethub.h */

/* the answer for the hook's stdin: "wifi=ok", or "wifi=fail,<code>" (LINK_ERR_*, 0 = no answer
 * from the BL616, -1 = the SDIO card did not come up) */
static void wifi_start(char *answer, size_t n)
{
    enum link_end e = link_cmd("WIFI", NULL);
    if (e != LINK_ACKED) {
        int code = e == LINK_NAKED ? (int)nak_code : 0;
        logmsg("WIFI %s: %s", link_end_name(e), code == LINK_ERR_NO_CREDENTIALS ? "no credentials stored, pair over BLE on USB power"
                                               : code == LINK_ERR_RADIO        ? "BL616 radio failed"
                                                                               : "no Wi-Fi");
        snprintf(answer, n, "wifi=fail,%d\n", code);
        return;
    }
    int fd = open("/dev/bl616", O_RDWR);
    bool up = fd >= 0 && ioctl(fd, BL616_IOCTL_RESCAN, NULL) == 0;
    if (!up)
        logmsg("BL616 SDIO rescan: %s", strerror(errno));
    if (fd >= 0)
        close(fd);
    snprintf(answer, n, up ? "wifi=ok\n" : "wifi=fail,-1\n");
}

/* ---------------- main ---------------- */

static int run_hook(const char *reason, unsigned *sleep_s)
{
    int pipefd[2], infd[2];                  /* hook's stdout to us, our answers to its stdin */
    if (pipe(pipefd) < 0)
        return -1;
    if (pipe(infd) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    pid_t hook_pid = fork();
    if (hook_pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(infd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        close(infd[0]);
        close(infd[1]);
        execl(hook, hook, reason, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    close(infd[0]);

    /* the hook may print "sleep=<seconds>" to choose the next wake-up, and a line "wifi" when it
     * needs the network: the BL616 never starts Wi-Fi on its own */
    char buf[128];
    ssize_t n;
    size_t used = 0, scanned = 0;
    char out[512] = "";
    bool killed = false;
    uint64_t deadline = now_ms() + HOOK_TIMEOUT_S * 1000ull;
    while (used < sizeof(out) - 1) {
        if (now_ms() > deadline) {
            logmsg("hook still running after %u s, killed", HOOK_TIMEOUT_S);
            kill(hook_pid, SIGKILL);
            killed = true;
            break;
        }
        struct pollfd p = { .fd = pipefd[0], .events = POLLIN };
        int rc = poll(&p, 1, 200);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc == 0) {
            struct frame f;                 /* answer repeats (a lost ACK) while the hook runs */
            while (next_cmd(&f, 0)) {
            }
            continue;
        }
        n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        size_t k = (size_t)n < sizeof(out) - 1 - used ? (size_t)n : sizeof(out) - 1 - used;
        memcpy(out + used, buf, k);
        used += k;
        out[used] = 0;
        for (char *nl; (nl = memchr(out + scanned, '\n', used - scanned)) != NULL; scanned = nl + 1 - out) {
            if (nl - (out + scanned) == 4 && memcmp(out + scanned, "wifi", 4) == 0) {
                char answer[24];
                wifi_start(answer, sizeof(answer));
                if (write(infd[1], answer, strlen(answer)) < 0)
                    logmsg("answer to the hook: %s", strerror(errno));
            }
        }
    }
    close(pipefd[0]);
    close(infd[1]);
    int status = 0;
    waitpid(hook_pid, &status, 0);
    char *s = strstr(out, "sleep=");
    if (s && !killed)
        *sleep_s = (unsigned)strtoul(s + 6, NULL, 10);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "u:l:x:")) != -1) {
        switch (opt) {
            case 'u': uart_id = atoi(optarg); break;
            case 'l': alive_pin = atoi(optarg); break;
            case 'x': hook = optarg; break;
            default:
                fprintf(stderr, "usage: %s [-u uart] [-l gpio] [-x hook]\n", argv[0]);
                return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);                 /* a hook that exited early must not take us down */

    if (alive_open() < 0 || uart_open() < 0)
        return 1;
    own_offset = clock_offset_s();
    bat_mv = battery_read_mv();                 /* before READY: the answer to WAKE carries it */
    pthread_t hb;
    pthread_create(&hb, NULL, heartbeat_thread, NULL);

    char reason[32] = "cold";
    bool got_wake = false;
    struct frame f;
    for (int i = 0; i < READY_TRIES && !got_wake; i++) {
        enum link_end e = link_cmd("READY", NULL);
        if (e != LINK_ACKED) {
            logmsg("READY %s", link_end_name(e));
            continue;
        }
        uint64_t until = now_ms() + READY_RETRY_MS;
        while (!got_wake && now_ms() < until && next_cmd(&f, (int)(until - now_ms()))) {
            if (strcmp(f.cmd, "WAKE") == 0) {    /* <reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv> */
                char *time_s = strchr(f.arg, ',');
                if (time_s)
                    *time_s++ = 0;
                snprintf(reason, sizeof(reason), "%.31s", f.arg);
                if (time_s) {
                    clock_from_bl616(time_s);
                    char *sens = strchr(time_s, ',');
                    if (sens)
                        sensors_from_bl616(sens + 1);
                }
                got_wake = true;
            }
        }
    }
    if (!got_wake) {
        logmsg("no WAKE from BL616, assuming cold start");
        sensors_from_bl616("-1,0,-1,-1");   /* the hook still gets every variable */
    }
    logmsg("wake reason: %s", reason);

    unsigned sleep_s = DEFAULT_SLEEP_S;
    int rc = run_hook(reason, &sleep_s);
    logmsg("hook exited %d, next wake in %u s", rc, sleep_s);

    /* real time learned during the session (NTP over Wi-Fi): hand it to the BL616, whose clock
     * then survives until the next battery change and corrects its crystal drift */
    if (ntp_synced()) {
        char t[16];
        snprintf(t, sizeof(t), "%ld", (long)time(NULL));
        enum link_end e = link_cmd("TIME", t);
        if (e != LINK_ACKED)
            logmsg("TIME %s", link_end_name(e));
    }

    char sl[16];
    snprintf(sl, sizeof(sl), "%u", sleep_s);
    enum link_end e = link_cmd("SLEEP", sl);
    if (e != LINK_ACKED)
        logmsg("SLEEP %s", link_end_name(e));

    /* done: the heartbeat stops and with it the session; the BL616 cuts the power */
    heartbeat_run = 0;
    pthread_join(hb, NULL);
    alive_set(0);
    logmsg("done, heartbeat stopped");
    return 0;
}
