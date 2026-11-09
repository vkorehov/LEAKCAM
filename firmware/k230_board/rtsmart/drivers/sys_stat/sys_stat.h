/*
 * /dev/sysstat: the kernel's memory for the product program's STATUS frame (DESIGN.md 5.3.10, tests/test136). One
 * read() of struct sys_stat; the kernel driver (sys_stat.c) and the program (k230_app) share this header.
 */
#ifndef LEAKCAM_SYS_STAT_H
#define LEAKCAM_SYS_STAT_H

#include <stdint.h>

struct sys_stat {
    uint64_t heap_total, heap_free;     /* the kernel heap (memheaps, msh `free`): all pools summed */
    uint64_t heap_min_free;             /* its lowest free since boot (pool - max used) */
    uint64_t page_total, page_free;     /* the page allocator (user processes), bytes */
};

#endif
