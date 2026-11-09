#ifndef BAG_TEST_TIM_H
#define BAG_TEST_TIM_H
#include "main.h"
typedef struct { unsigned id; } TIM_HandleTypeDef;
extern TIM_HandleTypeDef htim2, htim3;
#define TIM_CHANNEL_3 3U
#define TIM_CHANNEL_4 4U
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
#endif
