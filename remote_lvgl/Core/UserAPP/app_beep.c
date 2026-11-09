/**
  ******************************************************************************
  * @file           : app_beep.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : 异步蜂鸣器提示音控制任务
  * 
  * @note           任务优先级 : 普通 (Normal)
  * 
  * 主要功能：
  * 1. 挂起等待消息队列 `beepQueueHandle`，实现完全非阻塞的音频播放
  * 2. 统一定义并执行多种音频序列 (升调/降调/急促报警/低电量长鸣)
  *
  * 注意事项：
  * - 采用 RTOS `osDelay` 控制音符持续与间隔时间，绝不会卡死主控与通信链路。
  * - 播放完毕后有 50ms 强制延时，防止高频触发导致的声音粘连。
  ******************************************************************************
  */
#include "app_beep.h"
#include "app_vars.h"
#include "beep.h"

void Play_Beep(uint8_t type);
/* USER CODE BEGIN Header_StartBeeperTask */
/**
 * @brief Function implementing the BeeperTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartBeeperTask */
void App_Beep_Main(void)
{
  /* USER CODE BEGIN StartBeeperTask */

  uint8_t beep_type;
  /* Infinite loop */
  for (;;)
  {
    // 永久等待声音请求
    if (osMessageQueueGet(beepQueueHandle, &beep_type, NULL, osWaitForever) == osOK)
    {
      switch (beep_type)
      {
      case BEEP_SHORT: // 普通操作：滴一声
        BEEP_Set_Freq(2000);
        osDelay(100);
        BEEP_Off();
        break;

      case BEEP_CONNECT: // 连接：哆-来-咪 (升调)
        BEEP_Set_Freq(1000);
        osDelay(100);
        BEEP_Set_Freq(2000);
        osDelay(100);
        BEEP_Set_Freq(3000);
        osDelay(100);
        BEEP_Off();
        break;

      case BEEP_DISCONNECT: // 断开：咪-来-哆 (降调)
        BEEP_Set_Freq(3000);
        osDelay(100);
        BEEP_Set_Freq(2000);
        osDelay(100);
        BEEP_Set_Freq(1000);
        osDelay(100);
        BEEP_Off();
        break;

      case BEEP_ALARM_LOW_BAT: // 低电量：长鸣两声
        BEEP_Set_Freq(1000);
        osDelay(300);
        BEEP_Off();
        osDelay(100);
        BEEP_Set_Freq(1000);
        osDelay(300);
        BEEP_Off();
        break;

      case BEEP_ALARM_FAULT: // 故障：急促短鸣
        for (int i = 0; i < 3; i++)
        {
          BEEP_Set_Freq(2500);
          osDelay(80);
          BEEP_Off();
          osDelay(80);
        }
        break;

      default:
        BEEP_Off();
        break;
      }

      // 声音播放完后，稍微休息一下，避免连续声音粘连
      osDelay(50);
    }
  }
  /* USER CODE END StartBeeperTask */
}

