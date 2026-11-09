/**
  ******************************************************************************
  * @file           : E34.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : E34 (LoRa) 无线透传模块的配置与模式切换驱动
  * 
  * 主要功能：
  * 1. 控制 PE0/PE1 (M0/M1) 引脚在透传模式与配置模式间切换
  * 2. 提供了动态写入工作信道/设备地址的接口 (`E34_Set_Address`)
  *
  * 注意事项：
  * - AT 配置指令为阻塞式发送，在进入配对流程时会引发短暂的系统停顿，需 osDelay 配合。
  ******************************************************************************
  */
#include "usart.h"
#include "E34.h"
#include "st7789.h"
#include "cmsis_os.h"
// 初始化 M0/M1 引脚（PE0/PE1）
void E34_Mode_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}
void E34_Set_FullDuplex_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);   // M0=1
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_RESET); // M1=0
}

void E34_Set_Config_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET); // M0=1
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET); // M1=1
}
// 设置模块为普通透传模式
void E34_Set_Normal_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET); // M0=0
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_RESET); // M1=0
}

// 设置E34模块地址（addr: 0x0000~0xFFFF）
void E34_Set_Address(uint16_t addr)
{
    uint8_t cfg_cmd[6];

    // 进入配置模式
    E34_Set_Config_Mode();
    // 构造写参数命令（以 E34-2G4H11S 通用格式为例，具体格式请参考数据手册）
    cfg_cmd[0] = 0xC0;               
    cfg_cmd[1] = (addr >> 8) & 0xFF; 
    cfg_cmd[2] = addr & 0xFF;        
    cfg_cmd[3] = 0x18;               
    cfg_cmd[4] = 0x00;               
    cfg_cmd[5] = 0x40;               

    osDelay(50);

    // 发送配置命令

    HAL_UART_Transmit(&huart3, cfg_cmd, 6, HAL_MAX_DELAY);
    osDelay(50);
    // 读取配置以确认
    // char show_buf[32];
    // sprintf(show_buf, "SetAddr:%02X%02X", cfg_cmd[1], cfg_cmd[2]);
    // ST7789_ShowString(10, 200, show_buf, Font_7x10, WHITE, BLUE);
    uint8_t read_cmd[3] = {0xC1, 0xC1, 0xC1};

    HAL_UART_Transmit(&huart3, read_cmd, 3, HAL_MAX_DELAY);
    osDelay(50);
    E34_Set_FullDuplex_Mode(); // 切换回工作模式
}