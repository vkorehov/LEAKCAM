/*
 * BL616 <-> K230 line protocol on UART0 (BL616) / UART1 (K230), LINK_BAUD 8N1.
 *
 *   $<seq>,<CMD>[,<arg>...][;b=<mV>|;t=<unix s>]*<XX>\n
 *                                      seq = 0..255, counted by each sender on its own
 *                                      XX  = CRC-8 of the bytes between '$' and '*' (link_crc8),
 *                                            two hex digits
 *                                      ;b=<mV>: the K230 attaches the battery it measured (VBAT on
 *                                            ADC_1, -1 = its ADC failed) to every frame it sends, ACKs
 *                                            too; the BL616 takes it from after the last ';' and keeps
 *                                            it (battery.c) for the phone and the server (DESIGN.md
 *                                            5.3.9, tests/test046)
 *                                      ;t=<unix s>: the BL616 attaches its wall clock to every frame it
 *                                            sends, ACKs too, once it has one (the server's X-Time,
 *                                            DESIGN.md 5.3.12: no NTP); the K230 takes it from after the
 *                                            last ';' and sets its clock when it is more than 5 s off
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
 *                                     taken: the BL616 starts the NetHub bridge (SDIO device + its join with the
 *                                     stored credentials, ~1 s; the full firmware's capture.c), the K230 then binds
 *                                     the card (/dev/bl616 rescan) and takes its address by DHCP; the bridge stops
 *                                     with the session. NAK codes: NO_CREDENTIALS, RADIO
 *                 SENT,<uploaded>,<failed>,<skipped>
 *                                     a SHOOT finished (k230_app/media.c): pictures uploaded, failed, videos skipped
 *                                     (not built yet); the BL616 cuts the power on battery
 *                 USB,<line>          the USB relay: a line from the phone, for the BL616 (the setup firmware's
 *                                     provisioning, the full firmware's ID/PIN/SETUP); not the focus session's
 *                                     FOCUS lines: the K230 answers those itself (k230_app/focus.c), after the BL616
 *                                     said OK to the phone's PIN
 *                 STATUS,<uptime s>,<version>,<die x10>,<heap free>,<heap min free>,<pages free B>
 *                                     every 60 s (and at a job's start): the battery read again and
 *                                     carried as ;b= (DESIGN.md 5.3.9);
 *                                     the program's version, the die temperature (-1000 = none) and the kernel's
 *                                     memory (/dev/sysstat, 0 = none): the BL616 keeps them for its check-in
 *                                     (DESIGN.md 5.3.10, tests/test136)
 *                 DONE,<cam0>,<cam1>,<foc0>,<foc1>
 *                                     a leak check finished (k230_app/job.c, DESIGN.md 5.3.12): each camera same, light,
 *                                     changed, new, nocam or unfocused (DESIGN.md 5.3.17), then each camera's sharpness
 *                                     in % of its focus reference (-1: none yet); the BL616 keeps it for the check-in
 *                                     (job=), checks in at once on a change and cuts the power on battery
 * BL616 -> K230   USB,<line>          the USB relay: a line for the phone
 * BL616 -> K230   SHOOT,<kind>,<dev>-<unix s>,<cams>,<photos>,<gap_ms>,<secs>,<host>,<port>
 *                                     pictures / videos for the server (the check-in answer's capture, DESIGN.md
 *                                     5.3.14; bl616_full/capture.c): kind photo / video / leak; picture n (1..photos)
 *                                     from camera (n-1) % cams, the rounds gap_ms apart; each uploaded to
 *                                     http://<host>:<port>/v1/media/<dev>-<unix s>-p<n> with X-Taken (unix ms);
 *                                     answered by SENT
 * BL616 -> K230   CHECK,<reason>      a leak check now (leak = a wet probe, rtc = the daily check) while the K230 runs
 *                                     already (on USB); answered by a DONE when it is finished
 * BL616 -> K230   STATUS              send your STATUS now (the unplug: DESIGN.md 5.3.11): the K230 reads its battery
 *                                     and sends its STATUS frame at once, the fresh reading as its ;b=
 * BL616 -> K230   WAKE,<reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>,<bat_mv>
 *                                     reply to READY: the reasons, every one that applies,
 *                                     joined by '+' (cold, leak, rtc, usb, setup: "rtc+leak"; data for the
 *                                     program's log only: it runs the same on every power-up, DESIGN.md 5.3.8); the
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
