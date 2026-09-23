/*
 * leakcam_agent: K230 (Linux, little core) side of the BL616 power protocol.
 *
 *  - toggles K230 GPIO2 (net K230_ALIVE -> BL616 IO01) as a heartbeat, every 500 ms;
 *  - talks to the BL616 over UART1 (GPIO40 TXD / GPIO41 RXD), protocol in bl616_pwrmgr/k230_link.h;
 *  - runs the capture hook for the wake reason, then asks to be powered off;
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
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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

static void link_send(const char *cmd, const char *arg)
{
    char body[64], frame[80];
    snprintf(body, sizeof(body), arg ? "%s,%s" : "%s", cmd, arg);
    uint8_t sum = 0;
    for (const char *p = body; *p; p++)
        sum ^= (uint8_t)*p;
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, sum);
    if (write(uart_fd, frame, n) != n)
        logmsg("uart write: %s", strerror(errno));
    tcdrain(uart_fd);
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

/* returns true with cmd/arg filled when a valid frame arrives within timeout_ms */
static bool link_recv(char *cmd, size_t cmdlen, char *arg, size_t arglen, int timeout_ms)
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
            char *comma = strchr(line, ',');
            if (comma) *comma = 0;
            if (copy_field(cmd, cmdlen, line) && copy_field(arg, arglen, comma ? comma + 1 : ""))
                return true;
        }
    }
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
            /* keep reading BL616 frames while the hook runs: it may send SHUTDOWN */
            char c[16], a[16];
            if (link_recv(c, sizeof(c), a, sizeof(a), 0) && strcmp(c, "SHUTDOWN") == 0)
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

    char cmd[16], arg[16], reason[16] = "cold";
    bool got_wake = false;
    for (int i = 0; i < READY_TRIES && !got_wake && !shutdown_req; i++) {
        link_send("READY", NULL);
        uint64_t until = now_ms() + READY_RETRY_MS;
        while (now_ms() < until && link_recv(cmd, sizeof(cmd), arg, sizeof(arg), (int)(until - now_ms()))) {
            if (strcmp(cmd, "WAKE") == 0) {
                snprintf(reason, sizeof(reason), "%s", arg);
                got_wake = true;
                break;
            }
            if (strcmp(cmd, "SHUTDOWN") == 0)
                shutdown_req = 1;
        }
    }
    if (!got_wake)
        logmsg("no WAKE from BL616, assuming cold start");
    logmsg("wake reason: %s", reason);

    unsigned sleep_s = DEFAULT_SLEEP_S;
    if (!shutdown_req) {
        int rc = run_hook(reason, &sleep_s);
        logmsg("hook exited %d, next wake in %u s", rc, sleep_s);
    }

    if (!shutdown_req) {
        char s[16];
        snprintf(s, sizeof(s), "%u", sleep_s);
        link_send("SLEEP", s);
        /* ACK is informative; the BL616 cuts power HALT_TIMEOUT_MS after SLEEP regardless */
        if (!link_recv(cmd, sizeof(cmd), arg, sizeof(arg), 2000) || strcmp(cmd, "ACK") != 0)
            logmsg("no ACK for SLEEP");
    }
    make_safe_for_power_cut();
    link_send("HALTED", NULL);

    /* keep the heartbeat running: stopping it now would only make the BL616 log a spurious
     * "heartbeat lost" if its power cut is delayed */
    for (;;)
        pause();
}
