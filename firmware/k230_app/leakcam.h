/* the program's log lines, shared by its parts (leakcam.c) */
#ifndef LEAKCAM_H
#define LEAKCAM_H

/* "leakcam: <text>" on the console */
void logmsg(const char *fmt, ...);
/* a step, with its time since boot and the heap growth: "leakcam: <step>: t <ms>, heap +<B>" */
void step(const char *what);

#endif
