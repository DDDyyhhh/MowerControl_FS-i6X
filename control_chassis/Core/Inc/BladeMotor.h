#ifndef BLADE_MOTOR_H
#define BLADE_MOTOR_H

#include "blade_motor_config.h"
#include "stm32f4xx_hal.h"
#include "safety_ownership_gate.h"

HAL_StatusTypeDef BladeMotor_SetSpeed(float speed);
HAL_StatusTypeDef BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner, float speed);
void BladeMotor_Start(void);
void BladeMotor_Stop(void);
/* CAN/API 0..3 levels share the manual controller's production setpoints. */
static inline float BladeMotor_CommandFromGear(uint8_t gear)
{
#if BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_PWM
    return gear == 1U ? BLADE_PWM_DUTY_LEVEL_1_PERCENT :
        gear == 2U ? BLADE_PWM_DUTY_LEVEL_2_PERCENT :
        gear == 3U ? BLADE_PWM_DUTY_LEVEL_3_PERCENT : 0.0f;
#else
    return gear == 1U ? BLADE_FTESC_ERPM_LEVEL_1 :
        gear == 2U ? BLADE_FTESC_ERPM_LEVEL_2 :
        gear == 3U ? BLADE_FTESC_ERPM_LEVEL_3 : 0.0f;
#endif
}

#endif
