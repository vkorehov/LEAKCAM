/*
 * LEAKCAM BL616 crash recorder: every exception, watchdog hang, assert, stack overflow and malloc failure leaves a
 * record with a stack backtrace in HBN RAM (.hbn_noinit), which survives the reset; the next boot prints it
 * (crash_rec_report) and `crash` shows or clears it. The record names its build (image and version): decode on the
 * host against that build's ELF, which `make flash` keeps (host.mk):
 *   tools/power-bringup/crash_decode.py [elf] < console text
 * User 2026-10-31: hard-to-reproduce issues need backtraces; every firmware keeps this.
 */
#ifndef LEAKCAM_CRASH_REC_H
#define LEAKCAM_CRASH_REC_H

#include <stdbool.h>
#include <stdint.h>

void crash_rec_report(void);          /* after the console is up: the build, and the last crash's record if any */
void crash_rec_fed(void);             /* the watchdog feeder after each feed (wdg_guard.c): the hang detector's clock */

/* "leakcam-build <image> <version>": this build (leakcam_version.h, firmware/VERSION) */
extern const char crash_rec_build[];

/* the kept record for the server (DESIGN.md 5.3.3, tests/test128): sent over Wi-Fi only, by whichever firmware reaches
 * the API first (wifi_check.c), cleared only once the server confirmed it */
struct crash_rec_info {
    const char *image, *version, *task;
    uint32_t count, kind, uptime_ms, mcause, mepc, mtval, ra, sp, stage, n_bt;
    const uint32_t *bt;
    const char *msg;                 /* a fatal error's words, "" for the others (not NUL-terminated past 40 bytes) */
};
bool crash_rec_get(struct crash_rec_info *i);   /* false: no record kept */
void crash_rec_sent(uint32_t count);            /* the server has record #count: cleared, unless a newer one came */

/* fail fast: this firmware's fatal error, recorded with its words, then the reset (never returns) */
void crash_rec_fatal(const char *msg) __attribute__((noreturn));
/* a K230 failure the BL616 saw (session ended badly, the K230's FATAL line): recorded, no reset */
void crash_rec_k230(const char *msg);

#endif
