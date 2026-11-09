#ifndef BLADE_MOTOR_H
#define BLADE_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "blade_motor_config.h"
#include "safety_ownership_gate.h"

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1
} HAL_StatusTypeDef;

typedef struct
{
    struct
    {
        uint32_t Period;
    } Init;
} TIM_HandleTypeDef;

#define TIM_CHANNEL_2 2U

void BladeMotor_HostSetCompare(TIM_HandleTypeDef *timer,
                               uint32_t channel,
                               uint32_t compare);
#define __HAL_TIM_SET_COMPARE(timer, channel, compare) \
    BladeMotor_HostSetCompare((timer), (channel), (compare))
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);

uint32_t HAL_GetTick(void);

HAL_StatusTypeDef BladeMotor_SetSpeed(float speed);
HAL_StatusTypeDef BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner, float speed);
void BladeMotor_Start(void);
void BladeMotor_Stop(void);

#endif
