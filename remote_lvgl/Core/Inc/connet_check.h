// communication.h 或添加到现有头文件
#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include "stm32f4xx_hal.h"

// 通信状态检测
extern volatile uint32_t last_receive_timestamp;
extern volatile uint8_t communication_connected;
extern uint32_t current_time ;
#define COMMUNICATION_TIMEOUT_MS 3000

// 函数声明
uint8_t CheckCommunicationStatus(void);
uint32_t GetLastReceiveTimeAgo(void);

#endif
