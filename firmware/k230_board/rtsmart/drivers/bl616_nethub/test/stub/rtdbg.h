/* Host stand-in for rtdbg.h: every line goes to host_log(), which counts errors and warnings
 * so a test can check that a bad input was reported. Printed with NETHUB_TEST_VERBOSE=1. */
#include <rtthread.h>

#define DBG_ERROR   0
#define DBG_WARNING 1
#define DBG_INFO    2
#define DBG_LOG     3

void host_log(int level, const char *tag, const char *fmt, ...);

#undef LOG_E
#undef LOG_W
#undef LOG_I
#undef LOG_D
#define LOG_E(...) host_log(DBG_ERROR, DBG_TAG, __VA_ARGS__)
#define LOG_W(...) host_log(DBG_WARNING, DBG_TAG, __VA_ARGS__)
#define LOG_I(...) host_log(DBG_INFO, DBG_TAG, __VA_ARGS__)
#define LOG_D(...) do { } while (0)
