/* Host stand-in: the card and host structures as far as bl616_sdio.c reads them
 * (mmcsd_card.h / mmcsd_core.h of the k230_rtos_sdk). */
#ifndef TEST_STUB_MMCSD_CORE_H
#define TEST_STUB_MMCSD_CORE_H

#include <rtthread.h>

#define SDIO_MAX_FUNCTIONS 7

struct rt_mmcsd_host { int lock_depth; };
struct rt_sdio_function;
typedef void (rt_sdio_irq_handler_t)(struct rt_sdio_function *);

struct rt_mmcsd_card
{
    struct rt_mmcsd_host *host;
    rt_uint8_t sdio_function_num;
    struct rt_sdio_function *sdio_function[SDIO_MAX_FUNCTIONS + 1];
};

struct rt_sdio_function
{
    struct rt_mmcsd_card *card;
    rt_sdio_irq_handler_t *irq_handler;
    rt_uint8_t num;
    rt_uint8_t func_code;
    rt_uint16_t manufacturer;
    rt_uint16_t product;
    rt_uint32_t cur_blk_size;
};

void mmcsd_host_lock(struct rt_mmcsd_host *host);
void mmcsd_host_unlock(struct rt_mmcsd_host *host);

#endif
