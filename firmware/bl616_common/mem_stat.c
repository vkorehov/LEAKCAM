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
#include "lwip/stats.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"

static unsigned segs(const struct tcp_seg *s)
{
    unsigned n = 0;
    for (; s; s = s->next)
        n++;
    return n;
}

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
#if MEM_STATS && MEMP_STATS
    /* the BL616's own lwIP heap (MEM_SIZE, lwipopts_user.h) and TCP segments: after the K230's video through the bridge
     * a check-in's 225 B send timed out (tests/test148 2026-11-13) */
    printf("lwip: heap used %u max %u avail %u err %u, tcp seg used %u max %u err %u, pbuf used %u err %u\r\n",
           (unsigned)lwip_stats.mem.used, (unsigned)lwip_stats.mem.max, (unsigned)lwip_stats.mem.avail,
           (unsigned)lwip_stats.mem.err, (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->used,
           (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->max, (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->err,
           (unsigned)lwip_stats.memp[MEMP_PBUF]->used, (unsigned)lwip_stats.memp[MEMP_PBUF]->err);
    LOCK_TCPIP_CORE();
    for (struct tcp_pcb *p = tcp_active_pcbs; p; p = p->next)
        printf("lwip: tcp %s %u -> %u, unsent %u unacked %u ooseq %u, retries %u, idle %lu ms\r\n",
               tcp_debug_state_str(p->state), p->local_port, p->remote_port, segs(p->unsent), segs(p->unacked),
#if TCP_QUEUE_OOSEQ
               segs(p->ooseq),
#else
               0u,
#endif
               p->nrtx, (unsigned long)((tcp_ticks - p->tmr) * TCP_SLOW_INTERVAL));
    for (struct tcp_pcb *p = tcp_tw_pcbs; p; p = p->next)
        printf("lwip: tcp TIME_WAIT %u -> %u\r\n", p->local_port, p->remote_port);
    UNLOCK_TCPIP_CORE();
#endif
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_mem, mem, memory metrics for tests and leak detection);
#endif
