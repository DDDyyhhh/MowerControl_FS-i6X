#!/usr/bin/env python3
"""Exercise production clock/TIM initialization and EG2104 PWM writes on a host."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'control_chassis/Core'


def function(text, signature):
    start = text.index(signature + '\n{')
    opened = text.index('{', start)
    end, depth = opened + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


HEADER = r'''
#ifndef ACTUATOR_TEST_TIM_H
#define ACTUATOR_TEST_TIM_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct {
    uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload;
} TestTimerInit;
typedef struct {
    void *Instance;
    TestTimerInit Init;
    uint32_t compare[4], configured, started;
} TIM_HandleTypeDef;
typedef struct { uint32_t MasterOutputTrigger, MasterSlaveMode; } TIM_MasterConfigTypeDef;
typedef struct { uint32_t OCMode, Pulse, OCPolarity, OCFastMode; } TIM_OC_InitTypeDef;
#define TIM2 ((void *)2)
#define TIM3 ((void *)3)
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define TIM_TRGO_RESET 0U
#define TIM_MASTERSLAVEMODE_DISABLE 0U
#define TIM_OCMODE_PWM1 1U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 4U
#define TIM_CHANNEL_3 8U
#define TIM_CHANNEL_4 12U
#define __HAL_TIM_SET_COMPARE(t,c,v) test_compare((t),(c),(v))
extern TIM_HandleTypeDef htim2, htim3;
void test_compare(TIM_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
#endif
'''

FIXTURE = r'''
#include "tim.h"
#include "EG2104.h"
#include <assert.h>
#include <stdio.h>
TIM_HandleTypeDef htim2, htim3;
typedef struct {
    uint32_t OscillatorType, HSEState;
    struct { uint32_t PLLState, PLLSource, PLLM, PLLN, PLLP, PLLQ; } PLL;
} RCC_OscInitTypeDef;
typedef struct {
    uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider, APB2CLKDivider;
} RCC_ClkInitTypeDef;
#define RCC_OSCILLATORTYPE_HSE 1U
#define RCC_HSE_ON 1U
#define RCC_PLL_ON 1U
#define RCC_PLLSOURCE_HSE 1U
#define RCC_PLLP_DIV2 2U
#define RCC_CLOCKTYPE_HCLK 1U
#define RCC_CLOCKTYPE_SYSCLK 2U
#define RCC_CLOCKTYPE_PCLK1 4U
#define RCC_CLOCKTYPE_PCLK2 8U
#define RCC_SYSCLKSOURCE_PLLCLK 1U
#define RCC_SYSCLK_DIV1 1U
#define RCC_HCLK_DIV4 4U
#define RCC_HCLK_DIV2 2U
#define FLASH_LATENCY_5 5U
#define PWR_REGULATOR_VOLTAGE_SCALE1 1U
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(v) ((void)(v))
static RCC_OscInitTypeDef osc;
static RCC_ClkInitTypeDef clocks;
static unsigned post_init_calls;
static void Error_Handler(void) { assert(!"HAL initialization failed"); }
static HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *value) {
    osc = *value; return HAL_OK;
}
static HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *value, unsigned latency) {
    assert(latency == FLASH_LATENCY_5); clocks = *value; return HAL_OK;
}
void test_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value) {
    assert(channel <= TIM_CHANNEL_4 && channel % 4U == 0U);
    assert(value <= timer->Init.Period);
    timer->compare[channel / 4U] = value;
}
static HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *timer) {
    assert(timer->Instance == TIM2 || timer->Instance == TIM3); return HAL_OK;
}
static HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(
    TIM_HandleTypeDef *timer, TIM_MasterConfigTypeDef *config) {
    (void)timer;
    assert(config->MasterOutputTrigger == TIM_TRGO_RESET);
    assert(config->MasterSlaveMode == TIM_MASTERSLAVEMODE_DISABLE); return HAL_OK;
}
static HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(
    TIM_HandleTypeDef *timer, TIM_OC_InitTypeDef *config, uint32_t channel) {
    assert(config->OCMode == TIM_OCMODE_PWM1 && config->OCPolarity == TIM_OCPOLARITY_HIGH);
    test_compare(timer, channel, config->Pulse);
    timer->configured |= 1U << (channel / 4U); return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel) {
    assert(timer->configured & (1U << (channel / 4U)));
    timer->started |= 1U << (channel / 4U); return HAL_OK;
}
static void HAL_TIM_MspPostInit(TIM_HandleTypeDef *timer) {
    (void)timer; ++post_init_calls;
}
void Serial_Printf(int port, const char *format, ...) { (void)port; (void)format; }
'''

TEST = r'''
static void check_bridge(EG2104_HBridge_t *bridge, float duty) {
    EG2104_HBridge_Init(bridge);
    assert(bridge->current_speed == 0.0f && bridge->direction == 0U);
    assert(bridge->left_timer->compare[bridge->left_in_channel / 4U] == 0U);
    assert(bridge->right_timer->compare[bridge->right_in_channel / 4U] == 0U);
    assert(EG2104_HBridge_SetSpeed(bridge, duty) == HAL_OK);
    assert(bridge->left_timer->compare[bridge->left_in_channel / 4U] == 5040U);
    assert(bridge->right_timer->compare[bridge->right_in_channel / 4U] == 0U);
    EG2104_HBridge_Stop(bridge);
    assert(EG2104_HBridge_SetSpeed(bridge, -duty) == HAL_OK);
    assert(bridge->left_timer->compare[bridge->left_in_channel / 4U] == 0U);
    assert(bridge->right_timer->compare[bridge->right_in_channel / 4U] == 5040U);
    EG2104_HBridge_Stop(bridge);
    assert(bridge->left_timer->compare[bridge->left_in_channel / 4U] == 0U);
    assert(bridge->right_timer->compare[bridge->right_in_channel / 4U] == 0U);
    assert(EG2104_HBridge_SetSpeed(bridge, 0.0f) == HAL_OK);
}
int main(void) {
    SystemClock_Config(); MX_TIM2_Init(); MX_TIM3_Init();
    assert(osc.PLL.PLLSource == RCC_PLLSOURCE_HSE && clocks.SYSCLKSource == RCC_SYSCLKSOURCE_PLLCLK);
    uint32_t hclk = HSE_VALUE / osc.PLL.PLLM * osc.PLL.PLLN / osc.PLL.PLLP / clocks.AHBCLKDivider;
    uint32_t timer_clock = hclk / clocks.APB1CLKDivider * (clocks.APB1CLKDivider == 1U ? 1U : 2U);
    assert(timer_clock == 84000000U && post_init_calls == 2U);
    TIM_HandleTypeDef *timers[] = { &htim2, &htim3 };
    const uint32_t ioc_period[] = { IOC_TIM2_PERIOD, IOC_TIM3_PERIOD };
    for (unsigned i = 0; i < 2U; ++i) {
        assert(timers[i]->Init.CounterMode == TIM_COUNTERMODE_UP);
        uint32_t divider = (timers[i]->Init.Prescaler + 1U) * (timers[i]->Init.Period + 1U);
        assert(timer_clock == 10000U * divider);
        assert(timers[i]->Init.Period == ioc_period[i]);
    }
    assert(blade_height_bridge.left_timer == &htim3 && blade_height_bridge.right_timer == &htim3);
    assert(grass_bag_bridge.left_timer == &htim2 && grass_bag_bridge.right_timer == &htim2);
    check_bridge(&blade_height_bridge, LIFTER_SPEED);
    check_bridge(&grass_bag_bridge, GRASS_BAG_SPEED);
    assert(htim2.started == 12U && htim3.started == 12U);
    assert(htim2.compare[0] == 0U && htim2.compare[1] == 0U);
    puts("PASS actual RCC/TIM2/TIM3/EG2104: 84MHz -> 10kHz, IOC agrees, both +/-60% -> CCR5040/8400, startup/stop zero");
}
'''

main = (CORE / 'Src/main.c').read_text()
tim = (CORE / 'Src/tim.c').read_text()
lifter = (CORE / 'UserDev/lifter.c').read_text()
ioc = (ROOT / 'control_chassis/concrol.ioc').read_text()
conf = (CORE / 'Inc/stm32f4xx_hal_conf.h').read_text()
defines = '#define HSE_VALUE ' + re.search(r'#define\s+HSE_VALUE\s+(\d+U)', conf)[1] + '\n'
for timer in (2, 3):
    defines += f'#define IOC_TIM{timer}_PERIOD ' + re.search(rf'^TIM{timer}\.Period=(\d+)$', ioc, re.M)[1] + 'U\n'
for name in ('LIFTER_SPEED', 'GRASS_BAG_SPEED'):
    defines += re.search(rf'^#define {name}\s+\S+', lifter, re.M)[0] + '\n'
bridges = '\n'.join(re.findall(r'EG2104_HBridge_t (?:blade_height_bridge|grass_bag_bridge) = \{.*?\};', lifter, re.S))
assert len(re.findall(r'EG2104_HBridge_t', bridges)) == 2
source = (FIXTURE + defines + function(main, 'void SystemClock_Config(void)') +
          function(tim, 'void MX_TIM2_Init(void)') + function(tim, 'void MX_TIM3_Init(void)') + bridges + TEST)
with tempfile.TemporaryDirectory(prefix='mower-actuator-pwm-') as directory:
    work = Path(directory)
    (work / 'tim.h').write_text(HEADER)
    (work / 'serial_port.h').write_text('#define SERIAL_PORT_2 2\nvoid Serial_Printf(int, const char *, ...);\n')
    (work / 'test.c').write_text(source)
    for optimization in ('-O0', '-O2'):
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', optimization, '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I' + str(work), '-I' + str(CORE / 'Inc'),
                        str(work / 'test.c'), str(CORE / 'UserDev/EG2104.c'), '-lm', '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True, timeout=5,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
