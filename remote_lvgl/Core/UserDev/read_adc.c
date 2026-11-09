/**
  ******************************************************************************
  * @file           : read_adc.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : ADC 轮询采样辅助驱动
  * 
  * 主要功能：
  * 1. 提供阻塞式的指定通道单次采样接口 `Read_ADC_Channel`
  * 
  * 注意事项：
  * - 本系统主摇杆摇数据主要依靠 DMA 自动搬运 (`adc_dma_buffer`)，
  *   此文件提供的轮询函数仅适用于辅助通道的非实时测量。
  ******************************************************************************
  */
#include "main.h"
#include "read_adc.h"
#include "stm32f4xx_hal_adc.h"  
#include "usart.h"
uint16_t Read_ADC_Channel(ADC_HandleTypeDef* hadc, uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    HAL_ADC_ConfigChannel(hadc, &sConfig);

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, HAL_MAX_DELAY);
    uint16_t value = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);
    return value;
}





// void Send_Joystick_Data(uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry)
// {
//     // 1. 必须使用 static
//     static uint8_t joy_buf[10];

    

//     joy_buf[0] = 0xAB; // 帧头
//     joy_buf[1] = (lx >> 8) & 0xFF;
//     joy_buf[2] = lx & 0xFF;
//     joy_buf[3] = (ly >> 8) & 0xFF;
//     joy_buf[4] = ly & 0xFF;
//     joy_buf[5] = (rx >> 8) & 0xFF;
//     joy_buf[6] = rx & 0xFF;
//     joy_buf[7] = (ry >> 8) & 0xFF;
//     joy_buf[8] = ry & 0xFF;
//     joy_buf[9] = 0xBA; // 帧尾
    
//     // 3. 使用 DMA 发送
//     HAL_UART_Transmit_DMA(&huart3, joy_buf, 10);
// }