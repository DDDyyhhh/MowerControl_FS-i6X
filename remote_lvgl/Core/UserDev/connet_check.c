/**
  ******************************************************************************
  * @file           : connet_check.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : 遥控器与割草机通信心跳监测 (系统看门狗)
  * 
  * 主要功能：
  * 1. 基于 RTOS Tick 判断最后一次有效遥测数据的接收时间差
  * 2. 超过阈值 (`COMMUNICATION_TIMEOUT_MS`) 后自动拉低连接标志位
  ******************************************************************************
  */

#include "connet_check.h"
#include <stdio.h>

volatile uint32_t last_receive_timestamp = 0;
volatile uint8_t communication_connected = 0;
// 检查通信连接状态
uint8_t CheckCommunicationStatus(void) {
    uint32_t  current_time = HAL_GetTick();
    
    // 检查是否超时
    if (communication_connected && 
        (current_time - last_receive_timestamp) > COMMUNICATION_TIMEOUT_MS) {
        communication_connected = 0;
    }
    
    return communication_connected;
}


// 获取最后接收时间（毫秒）
uint32_t GetLastReceiveTimeAgo(void) {
    if (last_receive_timestamp == 0) {
        return 0xFFFFFFFF;  // 从未接收到数据
    }
    return HAL_GetTick() - last_receive_timestamp;
}
