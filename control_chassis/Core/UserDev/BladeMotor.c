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

#include <stdbool.h>

/*
 * Stop sequence: the first zero after the blade may be turning sends ERPM 0
 * (speed-loop brake) for BLADE_FTESC_STOP_BRAKE_MS, then the idle blade is
 * held with a zero-current release (0x15). A sensorless ESC kept in its ERPM 0
 * speed loop can twitch the blade on lifter EMI or deck jolts; a released ESC
 * does not drive the motor at all.
 */
typedef enum
{
    BLADE_FTESC_FRAME_NONE = 0,
    BLADE_FTESC_FRAME_ERPM,    /* 0x18: run (erpm > 0) or brake (erpm == 0) */
    BLADE_FTESC_FRAME_RELEASE  /* 0x15: current 0 */
} BladeFtescFrame_t;

static BladeFtescFrame_t s_last_frame;
static float s_last_can_erpm;
static uint32_t s_last_send_tick;
/* Diagnostic latch: last zero -> nonzero command transition.  A brief
 * nonzero pulse is otherwise overwritten by the 200 ms release keepalive. */
volatile uint32_t g_blade_nonzero_latch_count;
volatile uint32_t g_blade_nonzero_latch_tick;
volatile int32_t g_blade_nonzero_latch_erpm;
static bool s_prev_nonzero;
#if BLADE_FTESC_IDLE_RELEASE_ENABLED
/* Power-up and every accepted run frame: the next zero must brake first. */
static bool s_may_be_turning = true;
static bool s_brake_active;
static uint32_t s_brake_since;
#endif

static void BladeMotor_MarkMayBeTurning(void)
{
#if BLADE_FTESC_IDLE_RELEASE_ENABLED
    s_may_be_turning = true;
    s_brake_active = false;
#endif
}

static bool BladeMotor_ZeroUsesRelease(uint32_t now)
{
#if BLADE_FTESC_IDLE_RELEASE_ENABLED
    if (s_may_be_turning)
    {
        s_may_be_turning = false;
        s_brake_active = true;
        s_brake_since = now;
    }
    if (s_brake_active &&
        (uint32_t)(now - s_brake_since) >= BLADE_FTESC_STOP_BRAKE_MS)
    {
        s_brake_active = false;
    }
    return !s_brake_active;
#else
    (void)now;
    return false;
#endif
}

static uint8_t BladeMotor_SendRelease(void)
{
#if BLADE_FTESC_RELEASE_USES_DUTY
    return Flipsky_CAN_ReleaseMotor();
#else
    return Flipsky_CAN_Release();
#endif
}

static HAL_StatusTypeDef BladeMotor_SendZero(uint32_t now, bool force)
{
    bool release = BladeMotor_ZeroUsesRelease(now);
    BladeFtescFrame_t frame = release ? BLADE_FTESC_FRAME_RELEASE : BLADE_FTESC_FRAME_ERPM;

    s_prev_nonzero = false;
    if (!force && s_last_frame == frame && s_last_can_erpm == 0.0f &&
        (now - s_last_send_tick) < BLADE_FTESC_KEEPALIVE_MS)
    {
        return HAL_OK;
    }

    if ((release ? BladeMotor_SendRelease() : Flipsky_CAN_SetSpeedErpm(0.0f)) == 0U)
    {
        return HAL_ERROR; /* cache untouched: the next call retries */
    }

    s_last_frame = frame;
    s_last_can_erpm = 0.0f;
    s_last_send_tick = now;
    return HAL_OK;
}

static HAL_StatusTypeDef BladeMotor_SetSpeedRaw(float speed_erpm)
{
    uint32_t now = HAL_GetTick();

    if (!isfinite(speed_erpm) || speed_erpm < 0.0f)
    {
        (void)BladeMotor_SendZero(now, true);
        return HAL_ERROR;
    }

    if (speed_erpm == 0.0f)
    {
        s_prev_nonzero = false;
        return BladeMotor_SendZero(now, false);
    }

    if (!s_prev_nonzero)
    {
        g_blade_nonzero_latch_count++;
        g_blade_nonzero_latch_tick = now;
        g_blade_nonzero_latch_erpm = (int32_t)speed_erpm;
    }
    s_prev_nonzero = true;

    if (s_last_frame == BLADE_FTESC_FRAME_ERPM && s_last_can_erpm == speed_erpm &&
        (now - s_last_send_tick) < BLADE_FTESC_KEEPALIVE_MS)
    {
        return HAL_OK;
    }

    if (Flipsky_CAN_SetSpeedErpm(speed_erpm) == 0U)
    {
        return HAL_ERROR;
    }

    s_last_frame = BLADE_FTESC_FRAME_ERPM;
    s_last_can_erpm = speed_erpm;
    s_last_send_tick = now;
    BladeMotor_MarkMayBeTurning();
    return HAL_OK;
}

void BladeMotor_Start(void)
{
    Flipsky_CAN_Init();
    s_last_frame = BLADE_FTESC_FRAME_NONE;
    s_last_can_erpm = 0.0f;
    s_last_send_tick = 0U;
    BladeMotor_MarkMayBeTurning(); /* a reset may leave the blade coasting */
    (void)BladeMotor_SendZero(HAL_GetTick(), true);
}

void BladeMotor_Stop(void)
{
    (void)BladeMotor_SendZero(HAL_GetTick(), true);
}

#else
#error "Unsupported BLADE_MOTOR_DRIVER_USED"
#endif

HAL_StatusTypeDef BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner, float speed)
{
    bool nonzero_intent = isfinite(speed) && (speed > 0.0f);

    if (!SafetyOwnershipGate_AuthorizeBlade(owner, nonzero_intent))
    {
        speed = 0.0f;
    }

    return BladeMotor_SetSpeedRaw(speed);
}

HAL_StatusTypeDef BladeMotor_SetSpeed(float speed)
{
    return BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, speed);
}
