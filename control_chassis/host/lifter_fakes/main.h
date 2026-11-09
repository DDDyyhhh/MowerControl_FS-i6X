#ifndef BAG_TEST_MAIN_H
#define BAG_TEST_MAIN_H
#define __MAIN_H
#include <stdint.h>
#include <stdbool.h>
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
#endif
