/**
  ******************************************************************************
  * @file    uart.h
  * @brief   Header for uart.c module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef __UART_H
#define __UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
#include "stm32f4xx_hal.h"  // <--- 添加这一行！
extern UART_HandleTypeDef huart1;

/* Exported constants --------------------------------------------------------*/
#define UART1_TX_PIN                GPIO_PIN_9
#define UART1_RX_PIN                GPIO_PIN_10
#define UART1_GPIO_PORT             GPIOA
#define UART1_AF                    GPIO_AF7_USART1

/* Exported macro ------------------------------------------------------------*/
/* Exported functions prototypes ---------------------------------------------*/
void UART1_Init(void);
void UART_Send_Byte(uint8_t byte);
void UART_Send_Buffer(uint8_t *buffer, uint32_t length);
uint8_t UART_Receive_Byte(void);
int UART_printf(const char *format, ...);

/* Redefined stdio functions for printf/scanf */
int fputc(int ch, FILE *f);
int fgetc(FILE *f);

#ifdef __cplusplus
}
#endif

#endif /* __UART_H */
