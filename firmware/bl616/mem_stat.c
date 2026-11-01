#include "mem_stat.h"

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "mm.h"

static void count_block(struct mm_heap *heap, void *ptr, size_t size, void *user)
{
    struct mem_stat *m = user;

    (void)heap;
    (void)ptr;
    m->blocks++;
    m->used_bytes += size;
}

void mem_stat(struct mem_stat *m)
{
    m->free_bytes = kfree_size(0);
    m->largest = kmax_free_size(0);
    m->min_free = kmin_free_size();
    m->blocks = 0;
    m->used_bytes = 0;
    if (mm_walk_allocated_blocks(count_block, m) != 0)
        printf("mem: heap walk failed, blocks and used not counted\r\n");
    m->tasks = (unsigned)uxTaskGetNumberOfTasks();
    m->deleted = 0;
    TaskStatus_t *st = malloc(sizeof(*st) * (m->tasks + 4));
    if (st) {
        UBaseType_t n = uxTaskGetSystemState(st, m->tasks + 4, NULL);
        for (UBaseType_t i = 0; i < n; i++)
            if (st[i].eCurrentState == eDeleted)
                m->deleted++;
        free(st);
    }
}

#ifdef CONFIG_SHELL
#include "shell.h"

/* one line for test runners: `mem: free 61234 largest 40960 min 52000 blocks 312 used 98765 tasks 18 deleted 0` */
static int cmd_mem(int argc, char **argv)
{
    struct mem_stat m;

    (void)argc;
    (void)argv;
    mem_stat(&m);
    printf("mem: free %u largest %u min %u blocks %lu used %u tasks %u deleted %u\r\n", (unsigned)m.free_bytes,
           (unsigned)m.largest, (unsigned)m.min_free, (unsigned long)m.blocks, (unsigned)m.used_bytes, m.tasks,
           m.deleted);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_mem, mem, memory metrics for tests and leak detection);
#endif
