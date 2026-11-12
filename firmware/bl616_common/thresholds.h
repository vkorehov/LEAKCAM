/*
 * The leak thresholds, both firmwares (DESIGN.md 5.3.16, user 2026-11-10; tests/test142): the probe's comparator
 * reference (mV) and the humidity alarm (%RH). The server sets them per device: every check-in answer of the full
 * firmware carries "probe_mv" and "humid" (presence.c); a changed value is written to flash (easyflash "thr"), so a
 * power loss, a reset, an HBN wake and the setup firmware keep it. Until the server ever sent one: 1100 mV, 85 %RH.
 * The check-in reports the values in use (thmv, thrh): the server sees what the board applies (state, not events).
 */
#ifndef LEAKCAM_THRESHOLDS_H
#define LEAKCAM_THRESHOLDS_H

#include <stdbool.h>

#define THR_PROBE_MV      1100      /* between a pressed wet towel (0.95 V) and a dried salt film (1.17 V) (test034) */
#define THR_HUMID         85        /* %RH: a warm board only reads lower (DESIGN.md 5.3.15) */
#define THR_PROBE_MV_MIN  500
#define THR_PROBE_MV_MAX  1600      /* dry, the probe node sits at 1.65 V */
#define THR_PROBE_STEP    50        /* the comparator's reference: vio_sel x 50 mV */
#define THR_HUMID_MIN     50
#define THR_HUMID_MAX     99

/* once at boot, right after easyflash_init(): the stored values, the defaults where none or out of range */
void thresholds_load(void);
int thresholds_probe_mv(void);
int thresholds_humid(void);
/* the server's values (-1: not in the answer, kept); out of range ignored; written to flash only when changed.
 * true: the probe's changed (the caller re-arms the comparator) */
bool thresholds_set(long probe_mv, long humid);

#endif
