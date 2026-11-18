#ifndef __SR04_UART_H
#define __SR04_UART_H

#include "main.h"
#include <stdbool.h>

// 定义接收缓冲区大小
#define SR04_RX_BUF_SIZE 64
#define SR04_SENSOR_COUNT 4
#define SR04_SAMPLE_FRESH_MS 1000U
#define SR04_NO_DISTANCE_CM 250.0f

// 全局变量声明
// extern float sr04_distances[SR04_SENSOR_COUNT]; // 建议改为通过函数获取，或者加 volatile

// 函数声明
void SR04_UART_Init(void);
// void SR04_UART_IRQHandler_Callback(void); // 删除这个，不再需要手动调用
void SR04_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size); // 新增回调处理
void SR04_Process_Data(uint8_t *data, uint16_t len); // 新增处理函数
/* Fresh filtered frame, copied without waiting. Returns false without a
 * measured channel or at SR04_SAMPLE_FRESH_MS. A zero field in a mixed
 * frame is returned as 250 cm (no usable range) for the warning. */
bool SR04_ReadDistances(float distances[SR04_SENSOR_COUNT]);
/* Copy all last accepted ranges. True while complete frames are arriving,
 * even if every channel in the latest frame reported a zero field. */
bool SR04_ReadLastDistances(float distances[SR04_SENSOR_COUNT]);
/* Last accepted nonzero range, or 250 cm at startup. Legacy walking cap. */
float SR04_GetDistance(uint8_t index);

#endif