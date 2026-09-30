/* Host test: the credential check's failure reading and HTTP status parsing (wifi_check_calc.h) */
#include <stdio.h>
#include <string.h>

#include "wifi_check_calc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); \
                                      printf("\n"); fails++; } } while (0)

static void t_join_failure(void)
{
    uint16_t d = 0;

    CHECK(wchk_join_failure(0, 15, &d) == WCHK_WRONG_PASSWORD && d == 15, "4-way handshake timeout");
    CHECK(wchk_join_failure(0, 16, &d) == WCHK_WRONG_PASSWORD && d == 16, "group key handshake timeout");
    CHECK(wchk_join_failure(0, 14, &d) == WCHK_WRONG_PASSWORD && d == 14, "MIC failure");
    CHECK(wchk_join_failure(0, 23, &d) == WCHK_WRONG_PASSWORD && d == 23, "802.1X failure");
    CHECK(wchk_join_failure(15, 0, &d) == WCHK_WRONG_PASSWORD && d == 15, "SAE challenge failure");
    CHECK(wchk_join_failure(17, 0, &d) == WCHK_JOIN_FAILED && d == 17, "AP full is not a password problem");
    CHECK(wchk_join_failure(0, 3, &d) == WCHK_JOIN_FAILED && d == 3, "deauth leaving");
    CHECK(wchk_join_failure(0, 0, &d) == WCHK_JOIN_FAILED && d == 0, "no code at all");
}

static void t_http(void)
{
    const char *ok = "HTTP/1.1 204 No Content\r\n";
    CHECK(wchk_http_status(ok, strlen(ok)) == 204, "204");
    CHECK(wchk_http_status("HTTP/1.0 302 Found\r\n", 20) == 302, "captive redirect");
    CHECK(wchk_http_status("HTTP/1.1 200\r\n", 14) == 200, "no reason phrase");
    CHECK(wchk_http_status("HTTP/1.1 204", 12) == 204, "status line cut after the code");
    CHECK(wchk_http_status("HTTP/1.1 20", 11) == -1, "short");
    CHECK(wchk_http_status("HTTP/2 204 x", 12) == -1, "not HTTP/1.x");
    CHECK(wchk_http_status("HTTP/1.1 2x4 x", 14) == -1, "not digits");
    CHECK(wchk_http_status("HTTP/1.1 2045", 13) == -1, "four digits");
    CHECK(wchk_http_status("<html>login", 11) == -1, "not HTTP at all");
}

int main(void)
{
    t_join_failure();
    t_http();
    if (fails)
        printf("wifi_check: FAIL (%d)\n", fails);
    else
        printf("wifi_check: join failure reading and HTTP status line pass\n");
    return fails != 0;
}
