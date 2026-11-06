/*
 * /dev/sysstat: the kernel heap and the page allocator in one read() (sys_stat.h), for the product program's STATUS
 * frame (DESIGN.md 5.3.10). The SDK's canmv_misc device has the same numbers but brings NTP, soft I2C, touch and
 * more along: this is the part LEAKCAM uses.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <mmu.h>
#include <page.h>

#include "sys_stat.h"

static struct rt_device dev;

static rt_size_t stat_read(rt_device_t d, rt_off_t pos, void *buf, rt_size_t size)
{
    (void)d;
    (void)pos;
    if (size != sizeof(struct sys_stat))
        return 0;
    struct sys_stat s;
    memset(&s, 0, sizeof(s));
    struct rt_object_information *info = rt_object_get_information(RT_Object_Class_MemHeap);
    rt_enter_critical();
    for (rt_list_t *n = info->object_list.next; n != &info->object_list; n = n->next) {
        struct rt_memheap *mh = (struct rt_memheap *)rt_list_entry(n, struct rt_object, list);
        s.heap_total += mh->pool_size;
        s.heap_free += mh->available_size;
        s.heap_min_free += mh->pool_size - mh->max_used_size;
    }
    rt_exit_critical();
    size_t total, free;
    rt_page_get_info(&total, &free);
    s.page_total = (uint64_t)total * ARCH_PAGE_SIZE;
    s.page_free = (uint64_t)free * ARCH_PAGE_SIZE;
    memcpy(buf, &s, sizeof(s));
    return sizeof(s);
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops stat_ops = { RT_NULL, RT_NULL, RT_NULL, stat_read, RT_NULL, RT_NULL };
#endif

static int sys_stat_init(void)
{
    dev.type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    dev.ops = &stat_ops;
#else
    dev.read = stat_read;
#endif
    return rt_device_register(&dev, "sysstat", RT_DEVICE_FLAG_RDONLY);
}
INIT_DEVICE_EXPORT(sys_stat_init);
