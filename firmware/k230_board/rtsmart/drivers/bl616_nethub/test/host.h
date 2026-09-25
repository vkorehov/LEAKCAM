/*
 * Host test harness for the bl616_nethub driver: RT-Thread primitives (host_rt.c) and a
 * simulated BL616 SDU, SDIO core and WLAN manager (sim_bl616.c).
 *
 * Scheduling is lockstep: the driver's worker runs in its own thread, but only between two of
 * its rt_event_recv() calls, and only when the test calls host_step(). Everything else (probe,
 * interrupt, lwIP transmit) runs on the test thread while the worker is parked, so each test is
 * deterministic. The clock is virtual: a step with no event pending lets the worker's timeout
 * expire and advances the clock by it, as the real 1 s safety poll would.
 */
#ifndef NETHUB_TEST_HOST_H
#define NETHUB_TEST_HOST_H

#include <rtthread.h>
#include <drivers/sdio.h>
#include <wlan_dev.h>

#include "bl616_nethub.h"

/* ---- host_rt.c */
extern rt_tick_t host_tick;          /* virtual clock, ms */
extern int host_log_errors, host_log_warnings;
extern int host_fail_thread_create;
extern rt_int32_t host_last_timeout; /* timeout the worker last waited with */
extern rt_uint32_t host_last_events; /* events its last pass woke up for */

int host_worker_alive(void);
void host_step(void);                /* one worker pass; advances the clock only on a timeout */
void host_advance(rt_tick_t ms);

/* ---- sim_bl616.c: the BL616 SDU as the host sees it */
#define SIM_PORTS 4
#define SIM_LOG   1024

struct sim_msg { rt_uint8_t buf[2048]; rt_size_t len; rt_size_t xfer; unsigned port; };

struct sim
{
    /* registers */
    rt_uint8_t card_ready;       /* download buffer / 64 */
    rt_tick_t card_ready_at;     /* CARD_READY reads 0 before this time */
    rt_uint8_t status;
    rt_uint32_t io_port;
    rt_uint8_t out_ptr;
    rt_uint8_t host_ready;       /* written by the host */
    rt_uint8_t int_mask;
    unsigned int_acks;           /* writes of 0 to HOST_INT_STATUS */
    rt_uint8_t rd_bitmap, wr_bitmap;
    rt_uint8_t rd_len[SIM_PORTS][2];
    unsigned rd_len_hi_reads;

    /* upload ports (BL616 -> host) */
    rt_uint8_t up[SIM_PORTS][4096];
    rt_size_t up_xfer[SIM_PORTS];
    unsigned up_next;            /* BL616's next upload port */
    unsigned host_up_next;       /* port the host must read next */
    unsigned up_reads;

    /* download ports (host -> BL616) */
    int auto_arm;                /* hand a new buffer to a port as soon as it is used */
    unsigned dn_next;            /* port the host must write next */
    struct sim_msg dn[SIM_LOG];
    unsigned ndn;

    /* SDIO core */
    int enabled, irq_attached, block_size;
    unsigned violations;         /* protocol errors the host made */
};

extern struct sim sim;
extern struct rt_mmcsd_host sim_host;
extern struct rt_mmcsd_card sim_card;
extern struct rt_sdio_function sim_func;
extern struct rt_sdio_driver *sim_driver;

void sim_reset(rt_uint8_t out_ptr);
void sim_irq(void);                            /* the card interrupt, as the core delivers it */
int sim_upload(const void *msg, rt_size_t len); /* queue one upload message, 0 or -1 if full */
int sim_upload_raw(rt_uint16_t rd_len, int two_bytes, const void *data, rt_size_t xfer);
rt_size_t sim_ch_msg(rt_uint8_t *out, rt_uint8_t tag, rt_uint8_t flag, rt_uint8_t cu,
                     rt_uint8_t cl, const void *payload, rt_size_t n);

/* ---- sim_bl616.c: the WLAN manager */
#define SIM_EVENTS 64
struct sim_wlan
{
    struct rt_wlan_device *dev;
    const struct rt_wlan_dev_ops *ops;
    int registered, registrations;
    rt_wlan_mode_t mode;
    rt_err_t register_result;
    rt_wlan_dev_event_t ev[SIM_EVENTS];
    struct rt_wlan_info scan[SIM_EVENTS];
    unsigned nev;
    rt_uint8_t rx[SIM_LOG][1600];
    rt_size_t rx_len[SIM_LOG];
    unsigned nrx;
};
extern struct sim_wlan wl;

#endif
