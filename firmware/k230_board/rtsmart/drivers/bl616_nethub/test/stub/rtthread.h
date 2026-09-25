/* Host stand-in for the parts of RT-Thread the bl616_nethub driver uses: types and error codes
 * as in rtdef.h of the k230_rtos_sdk, primitives implemented by host_rt.c. */
#ifndef TEST_STUB_RTTHREAD_H
#define TEST_STUB_RTTHREAD_H

#include <stdint.h>
#include <string.h>

typedef int8_t rt_int8_t;
typedef int16_t rt_int16_t;
typedef int32_t rt_int32_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef int rt_bool_t;
typedef long rt_base_t;
typedef unsigned long rt_ubase_t;
typedef rt_base_t rt_err_t;
typedef rt_uint32_t rt_tick_t;
typedef rt_ubase_t rt_size_t;

#define RT_TRUE  1
#define RT_FALSE 0
#define RT_NULL  ((void *)0)

#define RT_EOK      0
#define RT_ERROR    1
#define RT_ETIMEOUT 2
#define RT_EFULL    3
#define RT_EEMPTY   4
#define RT_ENOMEM   5
#define RT_ENOSYS   6
#define RT_EBUSY    7
#define RT_EIO      8
#define RT_EINTR    9
#define RT_EINVAL   10

#define RT_ALIGN(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define RT_NAME_MAX 16

#define RT_IPC_FLAG_PRIO    0x01
#define RT_WAITING_FOREVER  -1
#define RT_EVENT_FLAG_OR    0x02
#define RT_EVENT_FLAG_CLEAR 0x04

/* 1 tick = 1 ms, so the test clock reads in milliseconds */
#define RT_TICK_PER_SECOND 1000

static inline void *rt_memset(void *s, int c, rt_ubase_t n) { return memset(s, c, n); }
static inline void *rt_memcpy(void *d, const void *s, rt_ubase_t n) { return memcpy(d, s, n); }
void rt_kprintf(const char *fmt, ...);

struct rt_object { char name[RT_NAME_MAX]; };
struct rt_device { struct rt_object parent; };

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms);
rt_err_t rt_thread_mdelay(rt_int32_t ms);

struct rt_thread { void (*entry)(void *); void *param; };
typedef struct rt_thread *rt_thread_t;
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *parameter), void *parameter,
                             rt_uint32_t stack_size, rt_uint8_t priority, rt_uint32_t tick);
rt_err_t rt_thread_startup(rt_thread_t thread);

struct rt_event { rt_uint32_t set; };
rt_err_t rt_event_init(struct rt_event *event, const char *name, rt_uint8_t flag);
rt_err_t rt_event_send(struct rt_event *event, rt_uint32_t set);
rt_err_t rt_event_recv(struct rt_event *event, rt_uint32_t set, rt_uint8_t opt, rt_int32_t timeout,
                       rt_uint32_t *recved);

struct rt_mutex { int held; };
rt_err_t rt_mutex_init(struct rt_mutex *mutex, const char *name, rt_uint8_t flag);
rt_err_t rt_mutex_take(struct rt_mutex *mutex, rt_int32_t time);
rt_err_t rt_mutex_release(struct rt_mutex *mutex);

/* the real macro puts a pointer into an init section; here the test calls it by this name */
#define INIT_COMPONENT_EXPORT(fn) int (*const rt_init_##fn)(void) = fn

#endif
