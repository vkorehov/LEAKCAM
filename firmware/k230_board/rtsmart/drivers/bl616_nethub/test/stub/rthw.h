/* Host stand-in: the test runs the worker in lockstep with the caller (host_rt.c), so there is
 * nothing to mask. */
#ifndef TEST_STUB_RTHW_H
#define TEST_STUB_RTHW_H

#include <rtthread.h>

rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);

#endif
