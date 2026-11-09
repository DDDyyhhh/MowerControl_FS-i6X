/**
  ******************************************************************************
  * @file           : app_Report.c
  * @author         : [Your Name/Team]
  * @version        : V1.1
  * @date           : 2026-03-11
  * @brief          : 系统状态上报与指示任务，负责 LED 交互与 telemetry 遥测回传
  *
  * @note           任务优先级: 普通 (Normal)
  * 
  * 主要功能：
  * 1. 周期性 (10Hz / 100ms) 轮询全局状态 `g_app.g_system_report_state`
  * 2. 控制 RGB 状态指示灯 (WS2812 或 PWM LED)：
  *    - 红闪：物理碰撞故障
  *    - 蓝闪：急停按下 / 通信连接中
  *    - 红慢闪：通信丢失
  *    - 绿常亮：正常手动模式
  *    - 白/青/粉：巡航状态
  * 3. 调用 `receiver_resport()` 将电量、锁定状态、档位等信息回传给遥控器
  *
  * 注意事项：
  * - Legacy A28/E34 telemetry is emitted only when that backend is selected.
  ******************************************************************************
  */

#include "app_context.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "serial_port.h"
#include "tricolor_led.h"
#include "cruise_control.h"
#include "pairing_receiver.h"
#include "connet_check.h"
#include "remote_backend.h"

/* USER CODE BEGIN Header_StartTask_Report */
/**
 * @brief Function implementing the Task_Report thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask_Report */
void StartTask_Report(void *argument)
{
  /* USER CODE BEGIN StartTask_Report */
  // 初始延时，等待系统稳定
  osDelay(1000);
  for (;;)
  {
    // 10Hz 刷新率 (100ms)
    osDelay(100);

    // ============================================================
    // 1. LED 状态管理 (优先级排布)
    // ============================================================
    
    // 优先级 1：通信丢失 (无论是否碰撞，只要失联就是最高危机)
    //if (!CheckCommunicationStatus() || g_app.g_system_report_state == REPORT_FAULT_COMM_LOST)
   // {
        // 通信丢失：红灯慢闪
        // RGB_LED_Blink(LED_1, RGB_COLOR_RED, 500);
        // RGB_LED_Blink(LED_2, RGB_COLOR_RED, 500);
   // }
    // 优先级 2：物理安全锁存 (急停/碰撞/倾翻等)
    //else
     if (g_app.g_collision_latched)
    {
        if (g_app.g_system_report_state == REPORT_FAULT_EMERGENCY_BTN)
        {
            RGB_LED_Blink(LED_1, RGB_COLOR_BLUE, 200); // 急停：蓝灯快闪
            RGB_LED_Blink(LED_2, RGB_COLOR_BLUE, 200);
        }
        else if (g_app.g_system_report_state == REPORT_FAULT_COLLISION)
        {
            RGB_LED_Blink(LED_1, RGB_COLOR_RED, 200); // 碰撞：红灯快闪
            RGB_LED_Blink(LED_2, RGB_COLOR_RED, 200);
        }
        else 
        {
            // 其他未定义的严重故障：默认红灯快闪警告
            RGB_LED_Blink(LED_1, RGB_COLOR_RED, 200);
            RGB_LED_Blink(LED_2, RGB_COLOR_RED, 200);
        }
    }
    // 优先级 3：巡航模式
    else if (Cruise_GetState() != CRUISE_OFF)
    {
        // 巡航：白灯常亮 (或设置你喜欢的颜色)
        RGB_LED_SetColor(LED_1, RGB_COLOR_WHITE);
        RGB_LED_SetColor(LED_2, RGB_COLOR_WHITE);
    }
    // 优先级 4：正常手动模式
    else
    {
        // 正常：绿灯常亮
        RGB_LED_SetColor(LED_1, RGB_COLOR_GREEN);
        RGB_LED_SetColor(LED_2, RGB_COLOR_GREEN);
    }

    // 执行 LED 闪烁逻辑 (计算时间差)
    RGB_LED_ProcessBlink();


    if (RemoteBackend_LegacyTelemetryEnabled())
    {
      receiver_resport();
    }
  }
  /* USER CODE END StartTask_Report */
}