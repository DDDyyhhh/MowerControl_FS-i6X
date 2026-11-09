/**
 ******************************************************************************
 * @file           : app_Comm.c
 * @author         : [Your Name/Team]
 * @version        : V1.1
 * @date           : 2026-03-11
 * @brief          : 无线通信接收任务，负责接收并解析遥控器下发的数据包
 *
 * @note           任务优先级: 较高 (Above Normal)
 *
 * 主要功能：
 * 1. 依赖 UART DMA + IDLE 中断，通过消息队列 (Queue_RxDataHandle) 异步接收数据
 * 2. 帧头帧尾协议解析：摇杆数据、按键/旋钮状态、动态配对数据
 * 3. 通信链路监控：2秒内未收到合法数据包将触发超时机制，重启底层 DMA 接收
 *
 * 注意事项：
 * - 数据解析采用滑动窗口/逐字节扫描，防止粘包或断帧导致的数据错位。
 * - DMA 超时重启时，执行了 Dummy Read 和错误标志位清除，防止 UART 硬件 ORE 死锁。
 ******************************************************************************
 */
#include "../../../experiments/ticket03/exp3_board.h"
#include "app_context.h"
#include "remote_backend.h"


/* USER CODE BEGIN Header_StartTask_Comm */
/**
 * @brief Function implementing the Task_Comm thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask_Comm */
void StartTask_Comm(void *argument)
{
  (void)argument;
  for (;;)
  {
#if EXP3_ENABLED
    exp3_board_poll(HAL_GetTick());
#endif
    RemoteBackend_Process();
  }
}