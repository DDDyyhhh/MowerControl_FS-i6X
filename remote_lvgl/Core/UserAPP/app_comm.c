/**
  ******************************************************************************
  * @file           : app_comm.c
  * @author         : [Your Name/Team]
  * @version        : V1.1
  * @date           : 2026-03-11
  * @brief          : 遥控器无线下行链路 (RX) 接收解析任务
  * 
  * @note           任务优先级 : 较高 (Above Normal)
  * 
  * 主要功能：
  * 1. 基于 DMA + IDLE 中断接收割草机发来的遥测包，通过 `uartRxQueueHandle` 处理
  * 2. 循环扫描解析两种数据帧：状态帧 (0xCC..0xDD) 与 动态配对帧 (0xAA..0xBB)
  * 3. 状态变化边沿检测 (Edge Detection)：当挡位、刀盘、故障码改变时触发提示音
  * 4. 封装了线程安全的 `Safe_UART_DMA_Send`
  *
  * 注意事项：
  * - 【防死机机制】通信超时后主动清除 ORE(过载) 错误，并强行重置软件接收指针。
  * - 【防粘包】多次遍历有效防止了由于无线模块合并转发造成的“粘包/半包”现象。
  ******************************************************************************
  */
#include "app_comm.h"
#include "app_vars.h"
#include "usart.h"
#include "beep.h"
#include "pairing_remote.h"
#include "beep.h"
#include "pairing_remote.h"
// 声明私有变量


extern volatile uint8_t flag_ACK ;
extern volatile uint8_t flag_CONFIRM;
extern volatile uint8_t flag_TEST_OK;
extern osSemaphoreId_t Sem_Uart3TxHandle;
/* USER CODE BEGIN Header_StartRxTask */
/**
 * @brief Function implementing the RxTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartRxTask */
void App_Comm_Rx_Main(void)
{
  /* USER CODE BEGIN StartRxTask */

  // 1. 延时防抖
  osDelay(500);

  // 2. 清除错误并开启 DMA
  __HAL_UART_CLEAR_OREFLAG(&huart3);
  HAL_UART_Receive_DMA(&huart3, rx_buffer, 255);
  __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
  UartPacket_t rx_packet;

  // 定义历史状态变量
  static uint8_t last_status[20] = {0};
  static uint8_t last_connected = 0;
  memset(last_status, 0, sizeof(last_status));

  static uint32_t disconnect_start_tick = 0;

  /* Infinite loop */
  for (;;)
  {
    // 2. 从队列取数据
    if (osMessageQueueGet(uartRxQueueHandle, &rx_packet, NULL, 1000) == osOK)
    {
      // 1. 重置断联计时器
      disconnect_start_tick = 0;

      // 调试变量更新 (保持你原有的逻辑)
      isr_rx_len = rx_packet.len;
      if (rx_packet.len <= 255)
        memcpy(isr_rx_buffer, rx_packet.data, rx_packet.len);

      // 标志位：本轮是否收到了有效的状态包 (用于触发后面的蜂鸣器逻辑)
      uint8_t got_valid_status = 0;

      // ============================================================
      // 【核心修改】使用循环扫描，解决粘包和参数传递错误
      // ============================================================
      for (uint16_t i = 0; i < rx_packet.len; i++)
      {
        // ---------------------------------------------------------
        // A. 状态数据包 (0xCC ... 0xDD) - 判断 23 偏移量 (总长 24 字节)
        // ---------------------------------------------------------
        // 检查当前字节 i 是否为 0xCC，且 i+23 是否为 0xDD
        if (rx_packet.data[i] == 0xCC && (i + 23 < rx_packet.len) && rx_packet.data[i + 23] == 0xDD)
        {
         uint8_t *pData = &rx_packet.data[i]; // 指向当前包头

            // 1. 先将数据解包到一个局部的临时数组中（栈内存，绝对线程安全）
            uint8_t temp_status[24] = {0};
            
            // 使用 memcpy 快速提取 22 字节有效数据 (从 pData[1] 到 pData[22])
            memcpy(temp_status, &pData[1], 22);

            // 2. 进入临界区，一次性原子更新全局 status 数组
            // 拷贝 22 字节在 STM32F4 上仅需几个时钟周期，绝对不会漏掉串口中断
            taskENTER_CRITICAL();
            memcpy(status, temp_status, 22);
            taskEXIT_CRITICAL();

            // 更新连接状态
            last_receive_timestamp = HAL_GetTick();
            communication_connected = 1;
            got_valid_status = 1; // 标记收到有效状态

            i += 23; // 优化：跳过已处理的字节
        }
        // ---------------------------------------------------------
        // B. 配对数据包 (0xAA ... 0xBB) - 变长
        // ---------------------------------------------------------
        else if (rx_packet.data[i] == 0xAA)
        {
          // 向后搜索包尾 0xBB (限制搜索范围 10字节)
          for (uint16_t j = 1; j < 10 && (i + j < rx_packet.len); j++)
          {
            if (rx_packet.data[i + j] == 0xBB)
            {
              // 【修正】传入当前包的起始地址 &rx_packet.data[i]
              // 【修正】传入当前包的实际长度 j + 1
              Pairing_Remote_Handle_Message(&rx_packet.data[i], j + 1);
              
              i += j; // 优化：跳过
              break;
            }
          }
        }
      } // end for loop

      

      // ============================================================
      // 状态变化检测逻辑 (Edge Detection)
      // ============================================================

      // // 1. 连接状态变化
      // if (communication_connected == 1 && last_connected == 0)
      // {
      //   Play_Beep(BEEP_CONNECT); // 刚连上
      // }
      // last_connected = 1; // 更新连接标志

      // 2. 割刀状态 (status[0]) 变化
      if (status[0] != last_status[0])
      {
        Play_Beep(BEEP_SHORT); // 锁定/解锁提示
      }

      // 3. 速度挡位 (status[1]) 变化
      if (status[1] != last_status[1])
      {
        Play_Beep(BEEP_SHORT); // 换挡提示
      }

      // 4. 割刀速度 (status[2]) 变化
      if (status[2] != last_status[2])
      {
        Play_Beep(BEEP_SHORT);
      }

      // 5. 刀盘高度 (status[5]) 变化
      // 考虑到高度可能是连续变化的数值，我们只在变化超过一定阈值或停止变化时响
      // 或者简单点：每次变化都响一声短的
      if (status[5] != last_status[5])
      {
        // Play_Beep(BEEP_SHORT); // 如果觉得太吵可以注释掉
      }

      // ============================================================
      // <<<---  草袋物理状态 (status[6]) 变化提示 --->>>
      // ============================================================
      // status[6] 包含了: Bit 0(开合), Bit 1(满载), Bit 2(未连接)
      // 只要任何一个物理状态发生改变，就会触发短鸣提示
      if (status[6] != last_status[6])
      {
        Play_Beep(BEEP_SHORT); 
      }

      // 6. 故障报警 (status[11])
      // 只有当故障码从 0 变为 非0 时报警
      if ((status[11] != 0 && last_status[11] == 0))
      {
        Play_Beep(BEEP_ALARM_FAULT);
      }

      //7.解锁弹窗提醒
      if (status[12] != 0 && last_status[12] == 0)
      {
        Play_Beep(BEEP_ALARM_FAULT);
      }

      // 7. 低电量报警 (status[3])
      // 逻辑：电量低于20，且之前大于等于20 (只报一次)
      if (status[3] < 20 && last_status[3] >= 20)
      {
        Play_Beep(BEEP_ALARM_LOW_BAT);
      }

      // === 更新历史状态 ===
      memcpy(last_status, status, 20); // 18 bytes of status data
    }
    else
    {

      // 1. 标记物理层断开 (用于 UI 判断)
      if (communication_connected == 1)
      {
        communication_connected = 0;
      }

      // 2. 【新增】启动断联计时器
      if (disconnect_start_tick == 0)
      {
        disconnect_start_tick = HAL_GetTick();
      }

      // 3. 判断是否“真正断开” (> 1500ms)
      // 只有超时超过 1.5秒，才认为真的断了，触发报警并更新状态
      if (last_connected == 1 && (HAL_GetTick() - disconnect_start_tick > 1500))
      {
        Play_Beep(BEEP_DISCONNECT); // 这时候再响
        last_connected = 0;         // 这时候才更新历史状态
      }

      // === 超时了 (1秒都没收到数据) ===
      // 1. 计数器自增 (告诉我们发生了一次重启)
      restart_cnt++;

      // 2. 停止接收，准备复位
      HAL_UART_AbortReceive(&huart3);

      // 3. 清除可能存在的 ORE 错误
      __HAL_UART_CLEAR_OREFLAG(&huart3);
      volatile uint32_t temp = huart3.Instance->DR;
      (void)temp;

      // 4. 【关键一步】同步重置软件指针！！！
      // 因为 HAL_UART_Receive_DMA 会把硬件计数器重置为 255
      // 我们必须把软件记录的位置也重置为 0
      last_dma_pos = 0;

      // (可选) 清空 Buffer，防止残留数据干扰
      memset(rx_buffer, 0, 255);

      // 5. 重新开启 DMA
      HAL_UART_Receive_DMA(&huart3, rx_buffer, 255);
    }
    osDelay(5);
  }
  /* USER CODE END StartRxTask */
}

void Safe_UART_DMA_Send(uint8_t *data, uint16_t len)
{
    // 1. 获取互斥锁，确保同一时间只有一个任务能使用串口3发起发送
    if (osMutexAcquire(UartTxMutexHandle, 20) == osOK)
    {
        // 2. 确保串口处于空闲状态
        if (huart3.gState == HAL_UART_STATE_READY)
        {
            // 3. 启动 DMA 发送 (非阻塞，函数瞬间返回)
            if (HAL_UART_Transmit_DMA(&huart3, data, len) == HAL_OK)
            {
                // 4. 挂起当前任务，释放 CPU，耐心等待 DMA 发送完成中断
                // 超时时间设为 50ms (普通无线串口包 50ms 绝对发得完，防死锁)
                if (osSemaphoreAcquire(Sem_Uart3TxHandle, 50) == osOK)
                {
                    g_tx_ok_cnt++; // 真正发送成功
                }
                else
                {
                    // 5. 万一硬件 DMA 卡死或发生异常，导致没有进入完成中断
                    // 必须强行终止传输，恢复串口的 READY 状态，防止永久死锁
                    HAL_UART_AbortTransmit(&huart3);
                    g_tx_fail_cnt++; 
                }
            }
            else
            {
                g_tx_fail_cnt++; // DMA 启动失败 (可能是参数错误或底层被占用)
            }
        }
        else
        {
            g_tx_fail_cnt++; // 串口状态仍为 BUSY (罕见，说明状态机错乱了)
        }
        
        // 6. 硬件彻底发送完毕 (或超时流产) 后，释放互斥锁，允许下一轮发送
        osMutexRelease(UartTxMutexHandle);
    }
    else
    {
        g_tx_fail_cnt++; // 获取互斥锁超时，记为丢包
    }
}