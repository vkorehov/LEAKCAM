/*
 * leakcam: the K230 product program (RT-Smart user space), started at boot through RTT_AUTO_EXEC_CMD.
 *
 * One image, the BL616 picks the job (DESIGN.md 5.3.8): the program says READY over the link, the BL616's WAKE answer
 * names the reason. setup: the USB setup relay (tests/test113). Any other reason: the normal session, not built yet
 * (the heartbeat stops at once and the BL616 cuts the power).
 *
 *  - K230_ALIVE (GPIO2 -> BL616 IO01) toggles every 500 ms while the program runs: the BL616 keeps the K230 powered
 *    while the edges come and cuts the power 2 s after the last one; there is no shutdown command;
 *  - the link: /dev/link (UART1, LINK_BAUD; the kernel's link_mux passes only valid frames here, the console text that
 *    shares the wire goes to the console), protocol in k230_link.h: every command answered ACK/NAK, resent after
 *    LINK_ACK_TIMEOUT_MS, LINK_TRIES sends;
 *  - the battery: VBAT through R59/R61 (VBAT/3) on ADC_1, readable only while the K230 runs (tests/test023), read at the
 *    start and sent with every frame as the attribute ;b=<mV> (k230_link.h): the BL616 keeps it for the phone and the
 *    server (DESIGN.md 5.3.9). On USB power it reads the charger's output rail, not the cell: the BL616 knows the power
 *    (PGOOD) and does not keep that one;
 *  - the USB setup relay: USB-C 1 is the K230's USB0, the CDC ACM device /dev/ttyGS0 (CherryUSB). Every line the phone
 *    sends goes to the BL616 as the command USB,<line>; every USB,<line> from the BL616 goes back to the phone as a line.
 *    The provisioning itself is the BL616's (the K230 only carries bytes): the lines carry the Wi-Fi credentials, so
 *    nothing here prints them.
 *
 * Instrumented (tests read it from the console, skill leakcam-test-metrics): every step logs "t <ms>" since the K230
 * started (CLOCK_MONOTONIC counts from boot) and the program's heap growth since its start ("heap +<B>": the program
 * break; musl has no mallinfo): at the start, at the WAKE, at the relay start and every 16 relayed lines (a leak shows as
 * growth with the line count; the kernel's own heap is the console's `free`, read by the test before and after).
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
#include <time.h>
#include <unistd.h>

#include "k230_link.h"

#define HEARTBEAT_HALF_PERIOD_MS 500
#define READY_RETRY_MS           1000
#define READY_TRIES              3       /* the BL616 is up before it powers us: no answer in ~5 s = fail fast, power off (battery) */
#define ALIVE_PIN                2           /* K230 IO2 */
#define USB_DEV                  "/dev/ttyGS0"
#define USB_LINE                 (LINK_MAX_LINE - 24)   /* the line inside $<seq>,USB,<line>;b=<mV>*XX */
/* the battery (tests/test023): 12-bit ADC on VBAT/3, 1.8 V reference: 5400 mV full scale nominal, 5329 mV calibrated
 * against a meter on C15 (4.07-4.37 V); a mean of 64 conversions (one: sd 52 mV, 64: ~6 mV) */
#define BAT_ADC_CHANNEL          1
#define BAT_FULL_SCALE_MV        5329u
#define BAT_SAMPLES              64u
#define ADC_IOCTL_ENABLE         0

static int link_fd = -1, gpio_fd = -1;
static int bat_mv = -1;                 /* the battery read at the start, -1 = the ADC failed */
static volatile sig_atomic_t heartbeat_run = 1;

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "leakcam: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static uint64_t now_ms(void);

/* fail fast (product application standard): one line naming it, then the program ends; the heartbeat stops with it
 * and the BL616 cuts the power. The K230 crash recorder (DESIGN.md 5.3.3) is to come */
static void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "leakcam: FATAL: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static char *brk0;                      /* the program break at the start */

/* a step, with its time since boot and the heap growth: "leakcam: <step>: t <ms>, heap +<B>" */
static void step(const char *what)
{
    char *b = sbrk(0);
    if (!brk0)
        brk0 = b;
    logmsg("%s: t %llu ms, heap +%ld B", what, (unsigned long long)now_ms(), (long)(b - brk0));
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

/* ---------------- heartbeat ---------------- */

/* the RT-Smart GPIO device: one ioctl sets a pin's mode, then the file offset is the pin and one byte written is its
 * level (kernel ABI of bsp/maix3 drv_gpio, 'G' 0 = set mode, 0 = output) */
struct gpio_cfg {
    uint16_t pin;
    uint16_t value;
};
#define GPIO_IOCTL_SET_MODE _IOW('G', 0, struct gpio_cfg *)

static void alive_set(int v)
{
    uint8_t b = (uint8_t)v;
    if (lseek(gpio_fd, ALIVE_PIN, SEEK_SET) != ALIVE_PIN || write(gpio_fd, &b, 1) != 1)
        fatal("heartbeat: set GPIO%d: %s", ALIVE_PIN, strerror(errno));
}

static int alive_open(void)
{
    struct gpio_cfg c = { .pin = ALIVE_PIN, .value = 0 };
    gpio_fd = open("/dev/gpio", O_RDWR);
    if (gpio_fd < 0 || ioctl(gpio_fd, GPIO_IOCTL_SET_MODE, &c) != 0) {
        logmsg("GPIO%d as output: %s", ALIVE_PIN, strerror(errno));
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

/* ---------------- link: $<seq>,<CMD>[,args]*XX, every command answered ---------------- */

#define LINK_INBOX 4

struct frame {
    char cmd[16];
    char arg[LINK_MAX_LINE];
    unsigned seq;
};

static unsigned tx_next;                /* our next seq, 0..255 */
static int rx_last = -1;                /* last seq accepted from the BL616 */
static struct frame inbox[LINK_INBOX];  /* commands that arrived while we waited for an answer */
static unsigned in_head, in_count;

static int battery_read_mv(void)
{
    uint32_t raw, sum = 0;
    int fd = open("/dev/adc", O_RDWR);
    bool ok = fd >= 0 && ioctl(fd, ADC_IOCTL_ENABLE, BAT_ADC_CHANNEL) == 0;
    for (unsigned i = 0; ok && i < BAT_SAMPLES; i++) {
        ok = lseek(fd, BAT_ADC_CHANNEL, SEEK_SET) == BAT_ADC_CHANNEL &&
             read(fd, &raw, sizeof(raw)) == (ssize_t)sizeof(raw) && raw <= 4095;
        sum += raw;
    }
    if (fd >= 0)
        close(fd);
    if (!ok) {
        logmsg("battery: ADC channel %d: %s", BAT_ADC_CHANNEL, strerror(errno));
        return -1;
    }
    return (int)(((uint64_t)sum * BAT_FULL_SCALE_MV + 4095u * BAT_SAMPLES / 2) / (4095u * BAT_SAMPLES));
}

/* every frame carries the battery as its attribute (k230_link.h), ACKs too */
static void send_frame(unsigned seq, const char *cmd, const char *arg)
{
    char body[LINK_MAX_LINE], frame[LINK_MAX_LINE + 8];
    if (arg && *arg)
        snprintf(body, sizeof(body), "%u,%s,%s;b=%d", seq, cmd, arg, bat_mv);
    else
        snprintf(body, sizeof(body), "%u,%s;b=%d", seq, cmd, bat_mv);
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, link_crc8(body, body + strlen(body)));
    if (write(link_fd, frame, n) != n)
        fatal("link write: %s", strerror(errno));
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

/* true with f filled when a valid frame arrives within timeout_ms (0: what is buffered only) */
static bool read_frame(struct frame *f, int timeout_ms)
{
    static char line[LINK_MAX_LINE];
    static size_t len;
    static bool in_frame;
    uint64_t end = now_ms() + (uint64_t)timeout_ms;

    for (;;) {
        int left = (int)(end - now_ms());
        struct pollfd p = { .fd = link_fd, .events = POLLIN };
        int rc = poll(&p, 1, left < 0 ? 0 : left);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc <= 0)
            return false;
        char c;
        while (read(link_fd, &c, 1) == 1) {
            if (c == '$') { in_frame = true; len = 0; continue; }
            if (!in_frame || c == '\r') continue;
            if (c != '\n') {
                if (len < sizeof(line) - 1) line[len++] = c; else in_frame = false;
                continue;
            }
            in_frame = false;
            line[len] = 0;
            char *star = strchr(line, '*');
            if (!star || strtoul(star + 1, NULL, 16) != link_crc8(line, star))
                continue;
            *star = 0;
            char *end_seq;
            unsigned long seq = strtoul(line, &end_seq, 10);
            if (end_seq == line || *end_seq != ',' || seq > 255)
                continue;
            char *cmd = end_seq + 1, *comma = strchr(cmd, ',');
            if (comma)
                *comma = 0;
            if (*cmd && copy_field(f->cmd, sizeof(f->cmd), cmd) &&
                copy_field(f->arg, sizeof(f->arg), comma ? comma + 1 : "")) {
                f->seq = (unsigned)seq;
                return true;
            }
        }
    }
}

/* a command from the BL616: answered ACK; a repeat (our ACK was lost) is answered again, not delivered twice */
static bool accept_cmd(const struct frame *f)
{
    char s[8];
    snprintf(s, sizeof(s), "%u", f->seq);
    send_frame(f->seq, "ACK", s);
    if ((int)f->seq == rx_last)
        return false;
    rx_last = (int)f->seq;
    return true;
}

static void inbox_put(const struct frame *f)
{
    if (in_count == LINK_INBOX)
        fatal("link: inbox full at %s (the BL616 sends one command at a time)", f->cmd);
    inbox[(in_head + in_count++) % LINK_INBOX] = *f;
}

/* send a command and wait for its answer, resending; BL616 commands arriving meanwhile wait in the inbox */
static enum link_end link_cmd(const char *cmd, const char *arg)
{
    unsigned seq = tx_next;
    tx_next = (tx_next + 1) & 255;
    for (int t = 0; t < LINK_TRIES; t++) {
        send_frame(seq, cmd, arg);
        uint64_t end = now_ms() + LINK_ACK_TIMEOUT_MS;
        struct frame f;
        while (now_ms() < end) {
            if (!read_frame(&f, (int)(end - now_ms())))
                continue;
            unsigned long s = strtoul(f.arg, NULL, 10);
            if (!strcmp(f.cmd, "ACK") && s == seq)
                return LINK_ACKED;
            if (!strcmp(f.cmd, "NAK") && s == seq)
                return LINK_NAKED;
            if (strcmp(f.cmd, "ACK") && strcmp(f.cmd, "NAK") && accept_cmd(&f))
                inbox_put(&f);
        }
    }
    return LINK_TIMED_OUT;
}

/* the next command from the BL616 (the inbox first), within timeout_ms */
static bool next_cmd(struct frame *f, int timeout_ms)
{
    if (in_count) {
        *f = inbox[in_head];
        in_head = (in_head + 1) % LINK_INBOX;
        in_count--;
        return true;
    }
    uint64_t end = now_ms() + (uint64_t)timeout_ms;
    do {
        if (read_frame(f, (int)(end - now_ms())) && strcmp(f->cmd, "ACK") && strcmp(f->cmd, "NAK") && accept_cmd(f))
            return true;
    } while (now_ms() < end);
    return false;
}

/* ---------------- the USB setup relay ---------------- */

static void usb_relay(void)
{
    int usb = open(USB_DEV, O_RDWR | O_NONBLOCK);
    if (usb < 0) {
        fatal("open %s: %s", USB_DEV, strerror(errno));
    }
    step("setup: USB relay on " USB_DEV);
    unsigned lines = 0;
    char line[USB_LINE];
    size_t len = 0;
    bool too_long = false;
    for (;;) {
        struct frame f;
        while (next_cmd(&f, 0)) {               /* the BL616's answers to the phone */
            if (!strcmp(f.cmd, "USB")) {
                size_t n = strlen(f.arg);
                f.arg[n++] = '\n';
                if (write(usb, f.arg, n) != (ssize_t)n)
                    logmsg("setup: USB write short");   /* no phone listening: the line is dropped */
            }
        }
        struct pollfd p[2] = { { .fd = usb, .events = POLLIN }, { .fd = link_fd, .events = POLLIN } };
        if (poll(p, 2, 100) <= 0 || !(p[0].revents & POLLIN))
            continue;
        char buf[64];
        ssize_t n = read(usb, buf, sizeof(buf));
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r')
                continue;
            if (c != '\n') {
                if (len < sizeof(line) - 1)
                    line[len++] = c;
                else
                    too_long = true;
                continue;
            }
            line[len] = 0;
            if (too_long)
                (void)!write(usb, "ERR LONG\n", 9);
            else if (len && link_cmd("USB", line) != LINK_ACKED)
                fatal("setup: the BL616 did not take a line (link down)");
            if (len && ++lines % 16 == 0) {
                char what[32];
                snprintf(what, sizeof(what), "setup: %u lines", lines);
                step(what);
            }
            len = 0;
            too_long = false;
        }
    }
}

int main(void)
{
    link_fd = open("/dev/link", O_RDWR | O_NONBLOCK);
    if (link_fd < 0 || alive_open() != 0) {
        fatal("no link or GPIO: %s", strerror(errno));
    }
    pthread_t hb;
    pthread_create(&hb, NULL, heartbeat_thread, NULL);
    step("start");
    bat_mv = battery_read_mv();
    char bat[32];
    snprintf(bat, sizeof(bat), "battery %d mV", bat_mv);
    step(bat);

    struct frame f;
    bool woken = false;
    for (int t = 0; t < READY_TRIES && !woken; t++) {
        if (link_cmd("READY", NULL) != LINK_ACKED) {
            logmsg("READY not answered");
            continue;
        }
        woken = next_cmd(&f, READY_RETRY_MS * 3) && !strcmp(f.cmd, "WAKE");
    }
    if (!woken) {
        fatal("no WAKE from the BL616");
    }
    char reason[32];
    snprintf(reason, sizeof(reason), "%.*s", (int)strcspn(f.arg, ","), f.arg);
    char what[48];
    snprintf(what, sizeof(what), "WAKE %s", reason);
    step(what);
    if (strstr(reason, "setup"))
        usb_relay();
    else
        logmsg("no session program yet: ending");
    heartbeat_run = 0;                          /* the BL616 cuts the power 2 s after the last edge */
    pthread_join(hb, NULL);
    return 0;
}
