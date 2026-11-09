#ifndef LIFTER_TEST_TASK_H
#define LIFTER_TEST_TASK_H
#include "FreeRTOS.h"
#define taskENTER_CRITICAL() LifterTest_EnterCritical()
#define taskEXIT_CRITICAL() LifterTest_ExitCritical()
#endif
