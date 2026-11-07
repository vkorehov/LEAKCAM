/*
 * The leak detection job (DESIGN.md 5.3.8, 5.3.12): its own thread beside the USB relay. One check at the start and one
 * per CHECK from the BL616: both cameras captured, each compared with its history. The relay owns the link: it takes
 * the result here and sends DONE,<cam0>,<cam1> (k230_link.h).
 */
#ifndef LEAKCAM_JOB_H
#define LEAKCAM_JOB_H

#include <stdbool.h>
#include <stddef.h>

/* the thread, its first check at once */
void job_start(void);
/* one more check (the BL616's CHECK,<reason>: a probe or the RTC while the K230 runs) */
void job_request(const char *reason);
/* a finished check's result for DONE ("<cam0>,<cam1>", each same / light / changed / new); true once per check */
bool job_result(char *buf, size_t len);

#endif
