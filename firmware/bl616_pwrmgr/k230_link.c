#include "k230_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflb_uart.h"

static char line[LINK_MAX_LINE];
static unsigned len;
static bool in_frame;

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

    char *comma = strchr(line, ',');
    if (comma)
        *comma = 0;
    if (strlen(line) >= sizeof(out->cmd))
        return false;
    strcpy(out->cmd, line);
    out->has_arg = comma != NULL;
    out->arg = comma ? strtoul(comma + 1, NULL, 10) : 0;
    return true;
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
            if (parse(out))
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

void link_send(const char *cmd, const char *arg)
{
    char body[LINK_MAX_LINE], frame[LINK_MAX_LINE + 8];
    if (arg)
        snprintf(body, sizeof(body), "%s,%s", cmd, arg);
    else
        snprintf(body, sizeof(body), "%s", cmd);
    uint8_t sum = 0;
    for (char *p = body; *p; p++)
        sum ^= (uint8_t)*p;
    int n = snprintf(frame, sizeof(frame), "$%s*%02X\n", body, sum);
    for (int i = 0; i < n; i++)
        bflb_uart_putchar(uart(), frame[i]);
}
