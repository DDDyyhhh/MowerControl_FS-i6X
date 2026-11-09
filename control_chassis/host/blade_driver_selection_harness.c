#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "BladeMotor.h"

TIM_HandleTypeDef htim2 = {{8399U}};

static uint32_t g_now_ms;
static uint32_t g_compare;
static uint32_t g_pwm_start_count;
static uint32_t g_ftesc_init_count;
static uint32_t g_ftesc_send_count;
static float g_ftesc_last_erpm;

void BladeMotor_HostSetCompare(TIM_HandleTypeDef *timer,
                               uint32_t channel,
                               uint32_t compare)
{
    assert(timer == &htim2);
    assert(channel == TIM_CHANNEL_2);
    g_compare = compare;
}


HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer == &htim2);
    assert(channel == TIM_CHANNEL_2);
    g_pwm_start_count++;
    return HAL_OK;
}


uint32_t HAL_GetTick(void)
{
    return g_now_ms;
}

void Flipsky_CAN_Init(void)
{
    g_ftesc_init_count++;
}

uint8_t Flipsky_CAN_SetSpeedErpm(float erpm)
{
    g_ftesc_send_count++;
    g_ftesc_last_erpm = erpm;
    return 1U;
}

static SafetyOwnershipGateInput_t AllowedInput(uint32_t sequence)
{
    SafetyOwnershipGateInput_t input = {
        false,
        false,
        false,
        true,
        true,
        EXECUTION_OWNER_MANUAL_REMOTE,
        sequence,
        0U
    };
    return input;
}

int main(void)
{
    SafetyOwnershipGateInput_t input = AllowedInput(1U);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    BladeMotor_Start();

#if BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_PWM
    assert(g_pwm_start_count == 1U);
    assert(g_compare == 0U);
    assert(g_ftesc_init_count == 0U);
    assert(g_ftesc_send_count == 0U);

    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 1.0f) == HAL_OK);
    assert(g_pwm_start_count == 1U);
    assert(g_compare == 3780U);

    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 80.0f) == HAL_OK);
    assert(g_compare == 4620U);

    input.software_locked = true;
    SafetyOwnershipGate_Update(&input);
    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 50.0f) == HAL_OK);
    assert(g_compare == 0U);
    assert(g_pwm_start_count == 1U);
    assert(g_ftesc_init_count == 0U);
    assert(g_ftesc_send_count == 0U);
#elif BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_FTESC_CAN
    assert(g_ftesc_init_count == 1U);
    assert(g_ftesc_send_count == 1U);
    assert(g_ftesc_last_erpm == 0.0f);
    assert(g_pwm_start_count == 0U);

    g_now_ms = 10U;
    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 14000.0f) == HAL_OK);
    assert(g_ftesc_send_count == 2U);
    assert(g_ftesc_last_erpm == 14000.0f);

    g_now_ms = 100U;
    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 14000.0f) == HAL_OK);
    assert(g_ftesc_send_count == 2U);

    g_now_ms = 210U;
    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 14000.0f) == HAL_OK);
    assert(g_ftesc_send_count == 3U);

    input.software_locked = true;
    SafetyOwnershipGate_Update(&input);
    assert(BladeMotor_SetSpeedForOwner(EXECUTION_OWNER_MANUAL_REMOTE, 15500.0f) == HAL_OK);
    assert(g_ftesc_send_count == 4U);
    assert(g_ftesc_last_erpm == 0.0f);
    assert(g_pwm_start_count == 0U);
#else
#error "Harness requires a legal blade driver"
#endif

    puts("blade driver selection harness passed");
    return 0;
}
