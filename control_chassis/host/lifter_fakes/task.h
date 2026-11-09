#ifndef LIFTER_TEST_TASK_H
#define LIFTER_TEST_TASK_H
#include <assert.h>
extern unsigned lifter_test_critical_depth;
#define taskENTER_CRITICAL() (++lifter_test_critical_depth)
#define taskEXIT_CRITICAL() do { assert(lifter_test_critical_depth); --lifter_test_critical_depth; } while (0)
#endif
