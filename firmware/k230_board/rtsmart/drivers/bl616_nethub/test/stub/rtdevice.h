/* Host stand-in: only the completion of the RT-Thread device drivers is used. */
#ifndef TEST_STUB_RTDEVICE_H
#define TEST_STUB_RTDEVICE_H

#include <rtthread.h>

struct rt_completion { int done; };
void rt_completion_init(struct rt_completion *c);
rt_err_t rt_completion_wait(struct rt_completion *c, rt_int32_t timeout);
void rt_completion_done(struct rt_completion *c);

#endif
