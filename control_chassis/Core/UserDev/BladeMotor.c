#include "BladeMotor.h"

#include <math.h>

#if BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_PWM

#include "tim.h"

static uint8_t s_pwm_running;

static HAL_StatusTypeDef BladeMotor_PwmSetDuty(float duty_percent)
{
    uint32_t period = htim2.Init.Period + 1U;
    uint32_t pulse;

    if (!isfinite(duty_percent) || duty_percent < 0.0f ||
        duty_percent > 100.0f || period == 0U)
    {
        return HAL_ERROR;
    }

    if (duty_percent <= 0.0f)
    {
        pulse = 0U;
    }
    else if (duty_percent >= 100.0f)
    {
        pulse = period - 1U;
    }
    else
    {
        pulse = (uint32_t)((float)period * duty_percent / 100.0f);
        if (pulse >= period)
        {
            pulse = period - 1U;
        }
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, pulse);
    return HAL_OK;
}

static HAL_StatusTypeDef BladeMotor_PwmEnsureRunning(void)
{
    if (s_pwm_running == 0U)
    {
        if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK)
        {
            return HAL_ERROR;
        }
        s_pwm_running = 1U;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef BladeMotor_PwmSetZero(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);
    return BladeMotor_PwmEnsureRunning();
}

static HAL_StatusTypeDef BladeMotor_SetSpeedRaw(float requested_duty_percent)
{
    float applied_duty;

    if (!isfinite(requested_duty_percent) || requested_duty_percent < 0.0f)
    {
        (void)BladeMotor_PwmSetZero();
        return HAL_ERROR;
    }

    if (requested_duty_percent <= BLADE_PWM_DUTY_DEADZONE_PERCENT)
    {
        return BladeMotor_PwmSetZero();
    }

    applied_duty = requested_duty_percent;
    if (applied_duty < BLADE_PWM_DUTY_MIN_PERCENT)
    {
        applied_duty = BLADE_PWM_DUTY_MIN_PERCENT;
    }
    if (applied_duty > BLADE_PWM_DUTY_MAX_PERCENT)
    {
        applied_duty = BLADE_PWM_DUTY_MAX_PERCENT;
    }

    if (BladeMotor_PwmSetDuty(applied_duty) != HAL_OK)
    {
        (void)BladeMotor_PwmSetZero();
        return HAL_ERROR;
    }
    if (BladeMotor_PwmEnsureRunning() != HAL_OK)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);
        return HAL_ERROR;
    }
    return HAL_OK;
}

void BladeMotor_Start(void)
{
    (void)BladeMotor_PwmSetZero();
}

void BladeMotor_Stop(void)
{
    (void)BladeMotor_PwmSetZero();
}

#elif BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_FTESC_CAN

#include "flipsky_can.h"

static float s_last_can_erpm = -1.0f;
static uint32_t s_last_send_tick;

static HAL_StatusTypeDef BladeMotor_SetSpeedRaw(float speed_erpm)
{
    uint32_t now;

    if (!isfinite(speed_erpm) || speed_erpm < 0.0f)
    {
        (void)Flipsky_CAN_SetSpeedErpm(0.0f);
        s_last_can_erpm = 0.0f;
        return HAL_ERROR;
    }

    now = HAL_GetTick();
    if (s_last_can_erpm == speed_erpm &&
        (now - s_last_send_tick) < BLADE_FTESC_KEEPALIVE_MS)
    {
        return HAL_OK;
    }

    if (Flipsky_CAN_SetSpeedErpm(speed_erpm) == 0U)
    {
        return HAL_ERROR;
    }

    s_last_can_erpm = speed_erpm;
    s_last_send_tick = now;
    return HAL_OK;
}

void BladeMotor_Start(void)
{
    Flipsky_CAN_Init();
    s_last_can_erpm = -1.0f;
    s_last_send_tick = 0U;
    (void)BladeMotor_SetSpeedRaw(0.0f);
}

void BladeMotor_Stop(void)
{
    (void)Flipsky_CAN_SetSpeedErpm(0.0f);
    s_last_can_erpm = 0.0f;
    s_last_send_tick = HAL_GetTick();
}

#else
#error "Unsupported BLADE_MOTOR_DRIVER_USED"
#endif

HAL_StatusTypeDef BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner, float speed)
{
    bool nonzero_intent = isfinite(speed) && (speed > 0.0f);

    if (!SafetyOwnershipGate_Authorize(owner, nonzero_intent))
    {
        speed = 0.0f;
    }

    return BladeMotor_SetSpeedRaw(speed);
}

HAL_StatusTypeDef BladeMotor_SetSpeed(float speed)
{
    return BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, speed);
}
