// /**
//   ******************************************************************************
//   * @file    uart.c
//   * @brief   UART driver implementation for STM32F4
//   ******************************************************************************
//   * @attention
//   *
//   * Copyright (c) 2025 STMicroelectronics.
//   * All rights reserved.
//   *
//   * This software is licensed under terms that can be found in the LICENSE file
//   * in the root directory of this software component.
//   * If no LICENSE file comes with this software, it is provided AS-IS.
//   *
//   ******************************************************************************
//   */

// #include "uart.h"
// #include <stdarg.h>
// #include <stdio.h>
// #include "main.h"
// /* Private variables ---------------------------------------------------------*/
// extern UART_HandleTypeDef huart3;  // 改为使用 huart3

// /* Private function prototypes -----------------------------------------------*/
// static void UART3_GPIO_Init(void);

// /**
//   * @brief  Initialize UART3 peripheral
//   * @retval None
//   */
// void UART3_Init(void)
// {
//     /* GPIO initialization */
//     UART3_GPIO_Init();
    
//     /* UART3 parameter configuration */
//     huart3.Instance = USART3;
//     huart3.Init.BaudRate = 9600;
//     huart3.Init.WordLength = UART_WORDLENGTH_8B;
//     huart3.Init.StopBits = UART_STOPBITS_1;
//     huart3.Init.Parity = UART_PARITY_NONE;
//     huart3.Init.Mode = UART_MODE_TX_RX;
//     huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
//     huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    
//     if (HAL_UART_Init(&huart3) != HAL_OK)
//     {
//         Error_Handler();
//     }
// }

// /**
//   * @brief  Initialize UART3 GPIO pins
//   * @retval None
//   */
// static void UART3_GPIO_Init(void)
// {
//     GPIO_InitTypeDef GPIO_InitStruct = {0};
    
//     /* GPIO Ports Clock Enable */
//     __HAL_RCC_GPIOB_CLK_ENABLE();
//     __HAL_RCC_USART3_CLK_ENABLE();
    
//     /* USART3 GPIO Configuration
//     PB10     ------> USART3_TX
//     PB11     ------> USART3_RX */
//     GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
//     GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
//     GPIO_InitStruct.Pull = GPIO_NOPULL;
//     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
//     GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    
//     HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
// }

// /* 修改所有串口收发函数为 huart3 */
// void UART_Send_Byte(uint8_t byte)
// {
//     HAL_UART_Transmit(&huart3, &byte, 1, HAL_MAX_DELAY);
// }

// void UART_Send_Buffer(uint8_t *buffer, uint32_t length)
// {
//     HAL_UART_Transmit(&huart3, buffer, length, HAL_MAX_DELAY);
// }

// uint8_t UART_Receive_Byte(void)
// {
//     uint8_t data;
//     HAL_UART_Receive(&huart3, &data, 1, HAL_MAX_DELAY);
//     return data;
// }

// int UART_printf(const char *format, ...)
// {
//     char buffer[128];
//     va_list args;
//     int length;
    
//     va_start(args, format);
//     length = vsnprintf(buffer, sizeof(buffer), format, args);
//     va_end(args);
    
//     if (length > 0)
//     {
//         HAL_UART_Transmit(&huart3, (uint8_t*)buffer, length, HAL_MAX_DELAY);
//     }
    
//     return length;
// }

// int fputc(int ch, FILE *f)
// {
//     (void)f;
//     UART_Send_Byte((uint8_t)ch);
//     return ch;
// }

// int fgetc(FILE *f)
// {
//     (void)f;
//     return (int)UART_Receive_Byte();
// }

// // // 初始化 M0/M1 引脚（PE0/PE7）
// // void E34_Mode_GPIO_Init(void)
// // {
// //     GPIO_InitTypeDef GPIO_InitStruct = {0};
// //     __HAL_RCC_GPIOE_CLK_ENABLE();

// //     GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_7;
// //     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
// //     GPIO_InitStruct.Pull = GPIO_NOPULL;
// //     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
// //     HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
// // }

// // // 设置模块为普通透传模式
// // void E34_Set_Normal_Mode(void)
// // {
// //     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET); // M0=0
// //     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7, GPIO_PIN_RESET); // M1=0
// // }

// // // 设置E34模块地址（addr: 0x0000~0xFFFF）
// // void E34_Set_Address(uint16_t addr)
// // {
// //     uint8_t cfg_cmd[6];

// //     // 进入配置模式
// //     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET); // M0=1
// //     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7, GPIO_PIN_SET); // M1=1
// //     HAL_Delay(10);

// //     // 构造写参数命令（以 E34-2G4H11S 通用格式为例，具体格式请参考数据手册）
// //     cfg_cmd[0] = 0xC0;                  // 写配置命令
// //     cfg_cmd[1] = (addr >> 8) & 0xFF;    // 高8位地址
// //     cfg_cmd[2] = addr & 0xFF;           // 低8位地址
// //     cfg_cmd[3] = 0x17;                  // 通道（默认23，0x17，可根据需要修改）
// //     cfg_cmd[4] = 0x44;                  // 速率等参数（可根据需要修改）
// //     cfg_cmd[5] = 0x00;                  // 其它参数（可根据需要修改）

// //     UART_Send_Buffer(cfg_cmd, 6);

// //     HAL_Delay(50); // 等待模块写入

// //     // 返回普通透传模式
// //     E34_Set_Normal_Mode();
// //     HAL_Delay(10);
// // }
