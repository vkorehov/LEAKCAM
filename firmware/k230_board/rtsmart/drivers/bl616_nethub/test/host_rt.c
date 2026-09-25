/* RT-Thread primitives on a host, lockstep scheduling of the driver's worker (see host.h). */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <rthw.h>
#include <rtdevice.h>
#include <rtdbg.h>

#include "host.h"

rt_tick_t host_tick;
int host_log_errors, host_log_warnings;
int host_fail_thread_create;
rt_int32_t host_last_timeout;
rt_uint32_t host_last_events;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static unsigned parks;          /* rt_event_recv() entries of the worker */
static unsigned grants;         /* passes the test allowed */
static int free_run;            /* rt_completion_wait(): let the worker run to its end */
static int alive;
static struct rt_event *worker_event;
static struct rt_thread thread;

static void die(const char *what)
{
    fprintf(stderr, "host_rt: %s\n", what);
    exit(2);
}

void host_log(int level, const char *tag, const char *fmt, ...)
{
    static int verbose = -1;
    va_list ap;

    if (level == DBG_ERROR)
        host_log_errors++;
    else if (level == DBG_WARNING)
        host_log_warnings++;
    if (verbose < 0)
        verbose = getenv("NETHUB_TEST_VERBOSE") != NULL;
    if (!verbose)
        return;
    printf("  [%c/%s] ", "EWIL"[level], tag);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void rt_kprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

rt_tick_t rt_tick_get(void) { return host_tick; }
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return (rt_tick_t)ms; }
void host_advance(rt_tick_t ms) { host_tick += ms; }

rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
    host_tick += (rt_tick_t)ms;
    return RT_EOK;
}

rt_base_t rt_hw_interrupt_disable(void) { return 0x5a; }

void rt_hw_interrupt_enable(rt_base_t level)
{
    if (level != 0x5a)
        die("interrupt level not restored");
}

rt_err_t rt_mutex_init(struct rt_mutex *m, const char *name, rt_uint8_t flag)
{
    (void)name;
    (void)flag;
    m->held = 0;
    return RT_EOK;
}

rt_err_t rt_mutex_take(struct rt_mutex *m, rt_int32_t time)
{
    (void)time;
    if (m->held)
        die("mutex taken twice (would deadlock)");
    m->held = 1;
    return RT_EOK;
}

rt_err_t rt_mutex_release(struct rt_mutex *m)
{
    if (!m->held)
        die("mutex released but not held");
    m->held = 0;
    return RT_EOK;
}

rt_err_t rt_event_init(struct rt_event *e, const char *name, rt_uint8_t flag)
{
    (void)name;
    (void)flag;
    e->set = 0;
    return RT_EOK;
}

rt_err_t rt_event_send(struct rt_event *e, rt_uint32_t set)
{
    pthread_mutex_lock(&mu);
    e->set |= set;
    pthread_mutex_unlock(&mu);
    return RT_EOK;
}

rt_err_t rt_event_recv(struct rt_event *e, rt_uint32_t set, rt_uint8_t opt, rt_int32_t timeout,
                       rt_uint32_t *recved)
{
    rt_uint32_t got;

    pthread_mutex_lock(&mu);
    worker_event = e;
    host_last_timeout = timeout;
    parks++;
    pthread_cond_broadcast(&cv);
    while (!grants && !free_run)
        pthread_cond_wait(&cv, &mu);
    if (grants)
        grants--;
    got = e->set & set;
    if (got && (opt & RT_EVENT_FLAG_CLEAR))
        e->set &= ~got;
    host_last_events = got;
    pthread_mutex_unlock(&mu);
    if (recved)
        *recved = got;
    return got ? RT_EOK : -RT_ETIMEOUT;
}

void rt_completion_init(struct rt_completion *c) { c->done = 0; }

void rt_completion_done(struct rt_completion *c)
{
    pthread_mutex_lock(&mu);
    c->done = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

rt_err_t rt_completion_wait(struct rt_completion *c, rt_int32_t timeout)
{
    (void)timeout;
    pthread_mutex_lock(&mu);
    free_run = 1;
    pthread_cond_broadcast(&cv);
    while (!c->done)
        pthread_cond_wait(&cv, &mu);
    while (alive)
        pthread_cond_wait(&cv, &mu);
    free_run = 0;
    pthread_mutex_unlock(&mu);
    return RT_EOK;
}

static void *thread_main(void *arg)
{
    struct rt_thread *t = arg;

    t->entry(t->param);
    pthread_mutex_lock(&mu);
    alive = 0;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return NULL;
}

rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *param,
                             rt_uint32_t stack_size, rt_uint8_t priority, rt_uint32_t tick)
{
    (void)name;
    (void)stack_size;
    (void)priority;
    (void)tick;
    if (host_fail_thread_create)
        return RT_NULL;
    if (alive)
        die("a second worker thread was created while one runs");
    thread.entry = entry;
    thread.param = param;
    return &thread;
}

/* starts the worker and returns once it waits for its first event */
rt_err_t rt_thread_startup(rt_thread_t t)
{
    pthread_t th;
    unsigned n;

    pthread_mutex_lock(&mu);
    n = parks;
    alive = 1;
    grants = 0;
    pthread_mutex_unlock(&mu);
    if (pthread_create(&th, NULL, thread_main, t) != 0)
        die("pthread_create");
    pthread_detach(th);
    pthread_mutex_lock(&mu);
    while (parks == n && alive)
        pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    return RT_EOK;
}

int host_worker_alive(void)
{
    int a;

    pthread_mutex_lock(&mu);
    a = alive;
    pthread_mutex_unlock(&mu);
    return a;
}

void host_step(void)
{
    unsigned n;

    pthread_mutex_lock(&mu);
    if (!alive)
        die("host_step() without a worker");
    /* nothing pending: the wait times out, and that much time passes */
    if (!worker_event->set && host_last_timeout > 0)
        host_tick += (rt_tick_t)host_last_timeout;
    n = parks;
    grants = 1;
    pthread_cond_broadcast(&cv);
    while (parks == n && alive)
        pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
}
