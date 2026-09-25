/*
 * BL616 <-> K230 line protocol on UART0 (BL616) / UART1 (K230), 115200 8N1.
 *
 *   $<seq>,<CMD>[,<arg>...]*<XX>\n     seq = 0..255, counted by each sender on its own
 *                                      XX  = XOR of the bytes between '$' and '*', two hex digits
 *
 * Every frame except ACK/NAK is a command and is answered with ACK,<seq> (accepted) or
 * NAK,<seq> (received but refused, e.g. a TIME outside the valid range). The sender keeps one
 * command in flight, resends it after LINK_ACK_TIMEOUT_MS without an answer and gives up after
 * LINK_TRIES sends; a NAK ends the command at once, it is not retried. A corrupted frame gets
 * no answer: its seq cannot be trusted, and the sender's timeout covers it. A receiver
 * remembers the last seq it accepted and its answer: a repeat (the answer was lost) gets the
 * same answer again and is not delivered twice. A READY restarts both sequence states (the agent
 * restarted), unless it repeats the READY just accepted.
 *
 * K230 -> BL616   READY               agent is up, wants the wake reason
 *                 SLEEP,<seconds>     work done: power me off, wake me in <seconds> (0 = leak only)
 *                 HALTED              filesystems synced / read-only, power may be cut
 *                 TIME,<unix s>       the K230 has real time (NTP): the BL616 takes it
 * BL616 -> K230   WAKE,<reason>,<unix s>  reply to READY: reason cold | leak | rtc, and the
 *                                     BL616 wall clock; 0 when it is not valid (after a power
 *                                     loss, until the K230 has sent TIME once)
 *                 ENV,<rh_x10>,<t_x10> after WAKE, when there is a fresh AHT20 sample
 *                 SHUTDOWN            please sync and send HALTED (low battery, session timeout)
 * both            ACK,<seq>   NAK,<seq>
 *
 * Anything that is not a valid frame (K230 boot noise, a partial line) is dropped silently.
 */
#ifndef LEAKCAM_K230_LINK_H
#define LEAKCAM_K230_LINK_H

#include <stdbool.h>
#include <stdint.h>

#define LINK_MAX_LINE       64
#define LINK_ACK_TIMEOUT_MS 300     /* a 64-byte frame takes 6 ms; the rest is the peer's loop */
#define LINK_TRIES          5
#define LINK_QUEUE          4       /* commands waiting behind the one in flight */

struct link_msg {
    char cmd[16];
    uint32_t arg;                   /* first argument as a number, 0 if none */
    bool has_arg;
    char args[40];                  /* everything after the command, as sent */
    uint8_t seq;
};

enum link_end { LINK_ACKED, LINK_NAKED, LINK_TIMED_OUT };
struct link_result {
    enum link_end end;
    char cmd[16];
};

void link_reset(void);
/* Decides ACK (true) or NAK (false) for each new command, once, before it is delivered; this
 * is where a refusable command is carried out. NULL = ACK everything. */
void link_set_verdict(bool (*verdict)(const struct link_msg *m));
/* Non-blocking. True when a new command arrived; it has already been answered. ACK/NAK frames
 * and duplicates are consumed here and never returned. */
bool link_poll(struct link_msg *out);
/* Queue a command that must be answered. False if the queue is full. */
bool link_send_cmd(const char *cmd, const char *arg);
/* Drive (re)sends; call from every wait loop. Returns how the command in flight ended (answered,
 * refused, or LINK_TRIES sends without an answer) once it has, else NULL. */
const struct link_result *link_service(uint64_t now_ms);
/* True while a command is queued or waiting for its ACK. */
bool link_busy(void);

#endif
