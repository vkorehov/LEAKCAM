/* Host stand-in for the RT-Thread SDIO core API used by bl616_sdio.c, same signatures as
 * components/drivers/include/drivers/sdio.h of the k230_rtos_sdk. The simulated BL616 SDU
 * behind them is sim_bl616.c. */
#ifndef TEST_STUB_SDIO_H
#define TEST_STUB_SDIO_H

#include <drivers/mmcsd_core.h>

#define SDIO_ANY_FUNC_ID 0xff
#define SDIO_ANY_MAN_ID  0xffff

struct rt_sdio_device_id
{
    rt_uint8_t func_code;
    rt_uint16_t manufacturer;
    rt_uint16_t product;
};

struct rt_sdio_driver
{
    char *name;
    rt_int32_t (*probe)(struct rt_mmcsd_card *card);
    rt_int32_t (*remove)(struct rt_mmcsd_card *card);
    struct rt_sdio_device_id *id;
    rt_size_t id_count;
};

rt_uint8_t sdio_io_readb(struct rt_sdio_function *func, rt_uint32_t reg, rt_int32_t *err);
rt_int32_t sdio_io_writeb(struct rt_sdio_function *func, rt_uint32_t reg, rt_uint8_t data);
rt_int32_t sdio_io_read_multi_fifo_b(struct rt_sdio_function *func, rt_uint32_t addr,
                                     rt_uint8_t *buf, rt_uint32_t len);
rt_int32_t sdio_io_write_multi_fifo_b(struct rt_sdio_function *func, rt_uint32_t addr,
                                      rt_uint8_t *buf, rt_uint32_t len);
rt_int32_t sdio_attach_irq(struct rt_sdio_function *func, rt_sdio_irq_handler_t *handler);
rt_int32_t sdio_detach_irq(struct rt_sdio_function *func);
rt_int32_t sdio_enable_func(struct rt_sdio_function *func);
rt_int32_t sdio_disable_func(struct rt_sdio_function *func);
rt_int32_t sdio_set_block_size(struct rt_sdio_function *func, rt_uint32_t blksize);
rt_int32_t sdio_register_driver(struct rt_sdio_driver *driver);

#endif
