/*
 * Memory metrics for every test and for leak detection (2026-10-24, user rule: memory metrics in all tests).
 *
 * A Wi-Fi join hung after a few cycles because tasks deleted by the Wi-Fi manager were never freed (IDLE starved by a
 * busy task): the free heap had been falling the whole time. mem_stat() makes that visible: free bytes, the largest
 * free block (fragmentation), the lowest free since boot, live allocations (count and bytes: the leak signal), tasks
 * and tasks deleted but not yet freed. The shell command `mem` prints one line a test runner logs before and after
 * every cycle of an operation; a steady fall of free bytes or a rise of blocks/deleted over N cycles is a leak
 * (tools/power-bringup/mem_leak.py).
 */
#ifndef LEAKCAM_MEM_STAT_H
#define LEAKCAM_MEM_STAT_H

#include <stddef.h>
#include <stdint.h>

struct mem_stat {
    size_t free_bytes;      /* all heaps */
    size_t largest;         /* largest free block: below what a big allocation needs = fragmentation */
    size_t min_free;        /* lowest free since boot (CONFIG_MM_ENABLE_MIN_FREE_TRACKING) */
    uint32_t blocks;        /* live allocations */
    size_t used_bytes;      /* their payload */
    unsigned tasks;         /* FreeRTOS tasks, including deleted ones not yet freed */
    unsigned deleted;       /* deleted, waiting for IDLE to free them: > 0 for long = IDLE starved */
};

void mem_stat(struct mem_stat *m);

#endif
