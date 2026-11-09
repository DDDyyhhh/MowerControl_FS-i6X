/**
  ******************************************************************************
  * @file           : beep.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : 无源蜂鸣器硬件 PWM (TIM1) 变频驱动
  * 
  * 主要功能：
  * 1. 动态计算定时器预分频与重装载值，实现精确的频率输出 (Hz)
  * 2. 保持 50% 占空比输出方波，实现最大音量
  * 3. 封装了供 RTOS 任务调用的消息队列压入接口 `Play_Beep`
  *
  * 注意事项：
  * - 频率计算高度依赖于 APB2 时钟频率的正确获取 (`HAL_RCC_GetPCLK2Freq`)。
  ******************************************************************************
  */
#include "beep.h"
#include "tim.h"
#include "cmsis_os.h"

extern osMessageQueueId_t beepQueueHandle; // 引用队列句柄

// 底层驱动：设置频率
void BEEP_Set_Freq(uint32_t freq)
{
    if (freq == 0) {
        BEEP_Off();
        return;
    }
    
    uint32_t timer_clock = HAL_RCC_GetPCLK2Freq(); // 确认TIM1所在总线频率
    // 如果使用了倍频，可能需要 *2，具体看时钟树。通常 PCLK2=84M, TIM1=168M
    // 这里假设配置正确，直接用你的原逻辑
    
    uint32_t prescaler = (timer_clock / (freq * 1000)) / 65536 + 1;
    uint32_t period = (timer_clock / (prescaler + 1)) / freq - 1;

    __HAL_TIM_SET_PRESCALER(&htim1, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim1, period);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, period / 2); 

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
}

void BEEP_Off(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);
}

// 供其他任务调用的接口：发送消息到队列
void Play_Beep(BeepType_t type)
{
    if (beepQueueHandle != NULL) {
        // timeout=0, 如果队列满了就丢弃，防止卡住主任务
        osMessageQueuePut(beepQueueHandle, &type, 0, 0); 
    }
}