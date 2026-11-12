#include "tls_link.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include <lwip/sockets.h>

#include "easyflash.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include "bflb_mtimer.h"
#include "mem_stat.h"
#include "wall_clock.h"

#define CRED_MAX 1024                /* one DER certificate (~460 B) or key (121 B) */

/* everything of one session on the heap, freed at its end: nothing held between check-ins */
struct sess {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca, crt;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int fd;
};
static struct sess *s;
static bool no_clock;                /* this handshake ran without a clock: its dates were not checked */

/* one easyflash blob on the heap (the caller frees it); NULL: absent */
static uint8_t *blob(const char *key, size_t *n)
{
    uint8_t *b = malloc(CRED_MAX);
    size_t saved = 0;
    if (!b)
        return NULL;
    *n = ef_get_env_blob(key, b, CRED_MAX, &saved);
    if (saved == 0 || saved > CRED_MAX) {
        free(b);
        return NULL;
    }
    return b;
}

bool tls_link_provisioned(void)
{
    size_t saved = 0;
    uint8_t x;
    return ef_get_env_blob("tls_ca", &x, 1, &saved) >= 0 && saved > 0 &&
           (saved = 0, ef_get_env_blob("tls_crt", &x, 1, &saved) >= 0 && saved > 0) &&
           (saved = 0, ef_get_env_blob("tls_key", &x, 1, &saved) >= 0 && saved > 0);
}

/* days since 1970-01-01 of a civil date (H. Hinnant's algorithm) */
static int64_t days(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t unix_s(const mbedtls_x509_time *t)
{
    return days(t->year, t->mon, t->day) * 86400 + t->hour * 3600 + t->min * 60 + t->sec;
}

/* every certificate of the chain: the dates by the wall clock once there is one, none on the first check without
 * one (DESIGN.md 5.3.12); mbedTLS itself has no time here (MBEDTLS_HAVE_TIME_DATE off), so the dates are ours */
static int verify(void *ctx, mbedtls_x509_crt *c, int depth, uint32_t *flags)
{
    (void)ctx;
    (void)depth;
    uint32_t now = wall_clock_now();
    if (!now)
        return 0;                    /* no clock: this answer's X-Time sets it */
    if ((int64_t)now < unix_s(&c->valid_from))
        *flags |= MBEDTLS_X509_BADCERT_FUTURE;
    if ((int64_t)now > unix_s(&c->valid_to))
        *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    return 0;
}

static int bio_send(void *ctx, const unsigned char *b, size_t n)
{
    int k = lwip_send(*(int *)ctx, b, n, 0);
    return k < 0 ? -0x4E : k;        /* MBEDTLS_ERR_NET_SEND_FAILED */
}

static int bio_recv(void *ctx, unsigned char *b, size_t n)
{
    int k = lwip_recv(*(int *)ctx, b, n, 0);
    return k < 0 ? -0x4C : k;        /* MBEDTLS_ERR_NET_RECV_FAILED (a timeout too: SO_RCVTIMEO) */
}

static size_t heap_free(void)
{
    struct mem_stat m;
    mem_stat(&m);
    return m.free_bytes;
}

static bool fail(const char *what, int err)
{
    printf("tls: %s -0x%04x\r\n", what, (unsigned)-err);
    tls_link_end();
    return false;
}

bool tls_link_start(int fd)
{
    static bool psa_up;
    uint64_t t0 = bflb_mtimer_get_time_ms();
    size_t free0 = heap_free();
    if (!psa_up) {
        if (psa_crypto_init() != PSA_SUCCESS) {
            printf("tls: PSA crypto did not start\r\n");
            return false;
        }
        psa_up = true;
    }
    s = calloc(1, sizeof(*s));
    if (!s) {
        printf("tls: no memory for a session\r\n");
        return false;
    }
    s->fd = fd;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca);
    mbedtls_x509_crt_init(&s->crt);
    mbedtls_pk_init(&s->key);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->drbg);

    int r = mbedtls_ctr_drbg_seed(&s->drbg, mbedtls_entropy_func, &s->entropy, (const unsigned char *)"leakcam", 7);
    if (r)
        return fail("random seed", r);
    size_t n;
    uint8_t *b = blob("tls_ca", &n);
    r = b ? mbedtls_x509_crt_parse_der(&s->ca, b, n) : -1;
    free(b);
    if (r) {
        printf("tls: no CA certificate stored (provision tls)\r\n");
        tls_link_end();
        return false;
    }
    b = blob("tls_crt", &n);
    r = b ? mbedtls_x509_crt_parse_der(&s->crt, b, n) : -1;
    free(b);
    if (r) {
        printf("tls: no device certificate stored (provision tls)\r\n");
        tls_link_end();
        return false;
    }
    b = blob("tls_key", &n);
    r = b ? mbedtls_pk_parse_key(&s->key, b, n, NULL, 0, mbedtls_ctr_drbg_random, &s->drbg) : -1;
    if (b) {
        memset(b, 0, CRED_MAX);      /* the private key's copy */
        free(b);
    }
    if (r) {
        printf("tls: no device key stored (provision tls)\r\n");
        tls_link_end();
        return false;
    }
    if ((r = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)))
        return fail("config", r);
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, NULL);
    mbedtls_ssl_conf_verify(&s->conf, verify, NULL);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->drbg);
    mbedtls_ssl_conf_min_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    if ((r = mbedtls_ssl_conf_own_cert(&s->conf, &s->crt, &s->key)))
        return fail("own certificate", r);
    if ((r = mbedtls_ssl_setup(&s->ssl, &s->conf)))
        return fail("setup", r);
    if ((r = mbedtls_ssl_set_hostname(&s->ssl, TLS_SERVER_NAME)))
        return fail("server name", r);
    mbedtls_ssl_set_bio(&s->ssl, &s->fd, bio_send, bio_recv, NULL);
    no_clock = wall_clock_now() == 0;
    while ((r = mbedtls_ssl_handshake(&s->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        uint32_t v = mbedtls_ssl_get_verify_result(&s->ssl);
        if (v && v != (uint32_t)-1)
            printf("tls: the server's certificate refused, flags 0x%lx\r\n", (unsigned long)v);
        return fail("handshake", r);
    }
    printf("tls: %s %s in %lu ms%s, session heap %u B\r\n", mbedtls_ssl_get_version(&s->ssl),
           mbedtls_ssl_get_ciphersuite(&s->ssl), (unsigned long)(bflb_mtimer_get_time_ms() - t0),
           no_clock ? ", no clock: dates not checked" : "", (unsigned)(free0 - heap_free()));
    return true;
}

int tls_link_send(const void *buf, size_t len)
{
    size_t done = 0;
    while (s && done < len) {
        int k = mbedtls_ssl_write(&s->ssl, (const unsigned char *)buf + done, len - done);
        if (k == MBEDTLS_ERR_SSL_WANT_READ || k == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (k <= 0)
            return k;
        done += (size_t)k;
    }
    return s ? (int)done : -1;
}

int tls_link_recv(void *buf, size_t len)
{
    for (;;) {
        if (!s)
            return -1;
        int k = mbedtls_ssl_read(&s->ssl, buf, len);
        if (k == MBEDTLS_ERR_SSL_WANT_READ || k == MBEDTLS_ERR_SSL_WANT_WRITE ||
            k == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)   /* TLS 1.3: a ticket before the answer */
            continue;
        if (k == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            return 0;
        return k;
    }
}

void tls_link_end(void)
{
    if (!s)
        return;
    (void)mbedtls_ssl_close_notify(&s->ssl);   /* best effort: the socket closes right after */
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->ca);
    mbedtls_x509_crt_free(&s->crt);
    mbedtls_pk_free(&s->key);
    mbedtls_ctr_drbg_free(&s->drbg);
    mbedtls_entropy_free(&s->entropy);
    free(s);
    s = NULL;
}
