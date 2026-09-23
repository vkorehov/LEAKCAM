/*
 * BL616 <-> K230 line protocol on UART0 (BL616) / UART1 (K230), 115200 8N1.
 *
 *   $<CMD>[,<arg>...]*<XX>\n     XX = XOR of the bytes between '$' and '*', two hex digits
 *
 * K230 -> BL616   READY               agent is up, wants the wake reason
 *                 SLEEP,<seconds>     work done: power me off, wake me in <seconds> (0 = leak only)
 *                 HALTED              filesystems synced / read-only, power may be cut
 * BL616 -> K230   WAKE,<reason>       reply to READY: cold | leak | rtc
 *                 SHUTDOWN            please sync and send HALTED (low battery, session timeout)
 *                 ACK,<cmd>
 *
 * Anything that is not a valid frame (K230 boot noise, a partial line) is dropped silently.
 */
#ifndef LEAKCAM_K230_LINK_H
#define LEAKCAM_K230_LINK_H

#include <stdbool.h>
#include <stdint.h>

#define LINK_MAX_LINE 64

struct link_msg {
    char cmd[16];
    uint32_t arg;
    bool has_arg;
};

void link_reset(void);
bool link_poll(struct link_msg *out);        /* non-blocking, true when a whole valid frame arrived */
void link_send(const char *cmd, const char *arg);

#endif
