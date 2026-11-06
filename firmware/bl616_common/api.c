#include "api.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

#include "crash_rec.h"
#include "device_id.h"
#include "leakcam_version.h"
#include "metrics.h"

/* the query value: unreserved characters as they are, every other byte %XX (an SSID is any 0-32 bytes) */
static void url_encode(char *out, size_t cap, const char *in, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 <= cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

/* the check-in's form: what the board is (the network, its image and version) and its metrics (metrics.h), written
 * when the request goes out, so this run's Wi-Fi figures are in it */
static char poll_ssid[3 * 32 + 1];

static size_t poll_form(char *form, size_t cap)
{
    char ver[3 * 36 + 1];
    /* the running image and version: the server offers an upgrade on it (DESIGN.md 5.3.7, tests/test130) */
    url_encode(ver, sizeof(ver), LEAKCAM_VERSION, strnlen(LEAKCAM_VERSION, 36));
    int n = snprintf(form, cap, "ssid=%s&" LEAKCAM_IMAGE "=%s", poll_ssid, ver);
    return n < 0 || (size_t)n >= cap ? cap : metrics_form(form, (size_t)n, cap);
}

void api_poll_target(struct wifi_check_target *t, char *path, const char *ssid, size_t ssid_len, char *body,
                     size_t body_cap)
{
    url_encode(poll_ssid, sizeof(poll_ssid), ssid, ssid_len);
    metrics_sample();                        /* the sensors before the radio starts: quiet rails */
    snprintf(path, API_PATH_CAP, "/v1/poll?id=%s", device_id());
    *t = (struct wifi_check_target){ API_HOST, API_PORT, path, 200, body, body_cap, true, NULL, poll_form, NULL, NULL };
}

/* at most 16 backtrace words: the request stays inside wifi_check.c's 768 bytes */
bool api_crash_target(struct wifi_check_target *t, char *path, char *form, uint32_t *count)
{
    struct crash_rec_info i;
    char img[3 * 12 + 1], ver[3 * 36 + 1], task[3 * 16 + 1], msg[3 * 40 + 1];

    if (!crash_rec_get(&i))
        return false;
    url_encode(img, sizeof(img), i.image, strnlen(i.image, 11));
    url_encode(ver, sizeof(ver), i.version, strnlen(i.version, 35));
    url_encode(task, sizeof(task), i.task, strnlen(i.task, 15));
    url_encode(msg, sizeof(msg), i.msg, strnlen(i.msg, 39));
    snprintf(path, API_PATH_CAP, "/v1/devices/%s/crash", device_id());
    int n = snprintf(form, API_CRASH_FORM_CAP,
                     "image=%s&ver=%s&n=%lu&k=%lu&t=%s&m=%s&up=%lu&c=%lx&pc=%lx&v=%lx&ra=%lx&sp=%lx&st=%lu&bt=", img, ver,
                     (unsigned long)i.count, (unsigned long)i.kind, task, msg,
                     (unsigned long)i.uptime_ms, (unsigned long)i.mcause, (unsigned long)i.mepc, (unsigned long)i.mtval,
                     (unsigned long)i.ra, (unsigned long)i.sp, (unsigned long)i.stage);
    for (uint32_t k = 0; k < i.n_bt && k < 16 && n > 0 && n + 10 < API_CRASH_FORM_CAP; k++)
        n += snprintf(form + n, API_CRASH_FORM_CAP - (size_t)n, k ? ",%lx" : "%lx", (unsigned long)i.bt[k]);
    if (n <= 0 || n >= API_CRASH_FORM_CAP)
        vAssertCalled();                     /* the fields' sizes are fixed: a build-time mistake */
    *count = i.count;
    *t = (struct wifi_check_target){ API_HOST, API_PORT, path, 200, NULL, 0, true, form };
    return true;
}
