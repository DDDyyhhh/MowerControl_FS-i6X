#ifndef BLADE_MOTOR_H
#define BLADE_MOTOR_H

#include "blade_motor_config.h"
#include "stm32f4xx_hal.h"
#include "safety_ownership_gate.h"

HAL_StatusTypeDef BladeMotor_SetSpeed(float speed);
HAL_StatusTypeDef BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner, float speed);
void BladeMotor_Start(void);
void BladeMotor_Stop(void);

#endif
