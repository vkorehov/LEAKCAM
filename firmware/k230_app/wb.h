/*
 * The cameras' white balance under the LEDs, kept for the next shot (wb.c; DESIGN.md 5.3.21): the ISP's automatic
 * white balance needs frames to follow the LEDs' light; its settled gains per camera are learnt, kept in the K230's
 * flash and put in at the start of every lit shot.
 */
#ifndef LEAKCAM_WB_H
#define LEAKCAM_WB_H

#include <stdbool.h>
#include <stdint.h>

/* the kept gains read from the flash: once, before the cameras' first lit shot */
void wb_load(void);
/* the LEDs go on (true: the next frames of each camera get its kept LED gains; with the LEDs on long enough the
 * automatic's settled gains are learnt) or off */
void wb_light(bool on);
/* every open camera has kept LED gains: a lit shot need not wait for the automatic white balance */
bool wb_known(void);
/* learnt gains written to the flash when they moved (the job's thread: not from the ISP's) */
void wb_keep(void);

#endif
