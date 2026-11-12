/* the program's log lines, shared by its parts (leakcam.c) */
#ifndef LEAKCAM_H
#define LEAKCAM_H

#include <stdbool.h>

/* "leakcam: <text>" on the console */
void logmsg(const char *fmt, ...);
/* a step, with its time since boot and the heap growth: "leakcam: <step>: t <ms>, heap +<B>" */
void step(const char *what);
/* one line to the phone on the cable (the newline added), from any thread; false: not all of it went */
bool usb_line(const char *s);

#endif
