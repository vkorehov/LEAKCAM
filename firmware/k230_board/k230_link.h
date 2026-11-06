/*
 * BL616 <-> K230 line protocol on UART0 (BL616) / UART1 (K230), LINK_BAUD 8N1.
 *
 *   $<seq>,<CMD>[,<arg>...][;b=<mV>]*<XX>\n
 *                                      seq = 0..255, counted by each sender on its own
 *                                      XX  = CRC-8 of the bytes between '$' and '*' (link_crc8),
 *                                            two hex digits
 *                                      ;b=<mV>: the K230 attaches the battery it measured (VBAT on
 *                                            ADC_1, -1 = its ADC failed) to every frame it sends, ACKs
 *                                            too; the BL616 takes it from after the last ';' and keeps
 *                                            it (battery.c) for the phone and the server (DESIGN.md
 *                                            5.3.9, tests/test046). The BL616's frames carry none
 *
 * Every frame except ACK/NAK is a command and is answered. The BL616 answers ACK,<seq>
 * (accepted) or NAK,<seq>,<code> (received but refused, code = LINK_ERR_*); the agent accepts
 * everything and answers ACK,<seq> only. The sender keeps one command in flight, resends it
 * after LINK_ACK_TIMEOUT_MS without an answer and gives up after LINK_TRIES sends; a NAK ends
 * the command at once, it is not retried. A corrupted frame gets
 * no answer: its seq cannot be trusted, and the sender's timeout covers it. A receiver
 * remembers the last seq it accepted and its answer: a repeat (the answer was lost) gets the
 * same answer again and is not delivered twice. A READY restarts both sequence states (the agent
 * restarted), unless it repeats the READY just accepted.
 *
 * K230 -> BL616   READY               agent is up, wants the wake reason
 *                 SLEEP,<seconds>     wake me next in <seconds> (0 = leak only; none = 6 h). The
 *                                     power goes off when the heartbeat stops, not on SLEEP
 *                 TIME,<unix s>       the K230 has real time (NTP): the BL616 takes it
 *                 WIFI                start Wi-Fi for this session; nothing else starts it. ACK:
 *                                     the radio is up and the BL616 joins with the stored
 *                                     credentials. NAK codes: NO_CREDENTIALS, RADIO
 *                 USB,<line>          the USB setup (WAKE setup): a line from the phone, for the BL616's provisioning
 *                 STATUS,<mode>,<uptime s>,<version>,<die x10>,<heap free>,<heap min free>,<pages free B>
 *                                     every 60 s (and at a job's start): the battery read again and
 *                                     carried as ;b= (DESIGN.md 5.3.9); mode = setup (the USB relay) or session
 *                                     (the normal product job): the BL616 sees which job runs (tests/test047);
 *                                     the program's version, the die temperature (-1000 = none) and the kernel's
 *                                     memory (/dev/sysstat, 0 = none): the BL616 keeps them for its check-in
 *                                     (DESIGN.md 5.3.10, tests/test048)
 * BL616 -> K230   USB,<line>          the USB setup: a line for the phone
 * BL616 -> K230   WAKE,<reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv>
 *                                     reply to READY: the reasons, every one that applies,
 *                                     joined by '+' (cold, leak, rtc, usb, humid: "rtc+leak"; setup alone: the K230
 *                                     runs the USB setup relay, DESIGN.md 5.3.8); the
 *                                     BL616 wall clock, 0 when it is not valid (after a power
 *                                     loss, until the K230 has sent TIME once); the AHT20 sample
 *                                     (rh -1 = the read failed, t then 0); the leak probe node voltage
 *                                     (1650 dry, below 1100 wet; -1 = no reading); the battery as
 *                                     the K230 measured it at the previous session (-1 = none
 *                                     since the power loss). The BL616 has no battery ADC input:
 *                                     the VBAT/3 divider (R59/R61, switched by the K230's 3V3)
 *                                     goes to K230 ADC_1 only.
 *                                     Answered ACK,<seq>,<bat_mv>: the K230's own reading now
 *                                     (-1 = its ADC failed), which the BL616 keeps for the next
 *                                     WAKE and its event log
 * both            ACK,<seq>[,<answer>]   NAK,<seq>,<code>
 *
 * Anything that is not a valid frame is the K230's console (its kernel's link_mux shares the wire since
 * 2026-10-22): k230_linesplit.h passes it to the BL616's second USB port (/dev/ttyACM1); the K230 splits what the
 * BL616 sends the same way (frames -> /dev/link, the rest -> msh).
 *
 * The session length is the K230's: it stays powered while its agent toggles K230_ALIVE
 * (GPIO2 -> BL616 IO01); the BL616 cuts the power once the edges stop. There is no shutdown
 * command in either direction.
 */
#ifndef LEAKCAM_K230_LINK_H
#define LEAKCAM_K230_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* CRC-8, polynomial 0x07, init 0, no reflection (CRC-8/SMBUS; "123456789" -> 0xF4). Unlike
 * an XOR sum it catches every 1- and 2-bit error and every burst up to 8 bits, including two
 * flips in the same bit position of different bytes and swapped bytes. */
static inline uint8_t link_crc8(const char *p, const char *end)
{
    uint8_t c = 0;
    for (; p < end; p++) {
        c ^= (uint8_t)*p;
        for (int k = 0; k < 8; k++)
            c = (uint8_t)(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

#define LINK_BAUD           2000000 /* tests/test039: 4 Mbaud lost ~0.5 % of short writes after idle, 2 Mbaud 0;
                                     * K230 side from here (k230_board/install.sh), U-Boot / dts / RT-Smart configs too */
#define LINK_MAX_LINE       160     /* a USB,<line> of the USB setup (DESIGN.md 5.3.8): PSK <63 hex-encoded> fits */
#define LINK_ACK_TIMEOUT_MS 300     /* a 64-byte frame takes 0.16 ms; the rest is the peer's loop */
#define LINK_TRIES          5
/* NAK codes; 0 is ACK */
#define LINK_ERR_REFUSED        1   /* invalid argument (TIME before 2026) */
#define LINK_ERR_NO_CREDENTIALS 2   /* WIFI: no SSID stored, pair over BLE on USB power first */
#define LINK_ERR_RADIO          3   /* WIFI: RF or storage failed to come up */

/* ---- the BL616 side (bl616_setup/k230_link.c); the K230 program has its own (k230_app/leakcam.c) ---- */

struct link_msg {
    char cmd[16];
    uint32_t arg;                   /* first argument as a number, 0 if none */
    bool has_arg;
    char args[LINK_MAX_LINE];       /* everything after the command, as sent */
    uint8_t seq;
};

enum link_end { LINK_ACKED, LINK_NAKED, LINK_TIMED_OUT };

struct link_result {                /* how a BL616 command ended: ACKED or TIMED_OUT */
    enum link_end end;
    char cmd[16];
    char answer[16];                /* what the ACK carried after its seq ("" = nothing) */
};

void link_reset(void);
/* Decides the answer to each new command, once, before it is delivered: 0 = ACK, else the LINK_ERR_* code sent with
 * NAK. NULL = ACK everything. */
void link_set_reply(uint8_t (*reply)(const struct link_msg *m));
/* Bytes from the wire: K230 console text goes to link_text(), valid frames are answered, ACK/NAK end the
 * command in flight, a new command is handed to on_cmd (accepted ones only, once). Frames are never logged: USB
 * lines carry the Wi-Fi credentials. */
void link_rx(const char *p, unsigned n, void (*on_cmd)(const struct link_msg *m));
/* Send a command that must be answered. False while the previous one is still in flight. */
bool link_send_cmd(const char *cmd, const char *arg);
/* Drive (re)sends; call from every wait loop. Returns how the command in flight ended (answered,
 * or LINK_TRIES sends without an answer) once it has, else NULL. */
const struct link_result *link_service(uint64_t now_ms);
/* bytes to the wire: the frames, and the host's keystrokes for the K230 console; given by the transport */
void link_tx(const char *p, unsigned n);
/* the K230's console text (everything that is not a frame); given by the transport */
void link_text(const char *p, unsigned n);

#endif
