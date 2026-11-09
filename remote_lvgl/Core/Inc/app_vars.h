#ifndef __APP_VARS_H
#define __APP_VARS_H

#include "main.h"
#include "cmsis_os.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdbool.h>

// --- 宏定义 (从 freertos.c 搬来) ---
#define REMOTE_BAT_MIN_MV 3600.0f
#define REMOTE_BAT_MAX_MV 4200.0f
#define ENABLE_DEBUG_PANEL 0 // 设置为1开启调试面板
#define CAR_WIDTH_BASE 79
#define CAR_HEIGHT_BASE 119

// --- RTOS 对象句柄 (extern) ---
extern osThreadId_t defaultTaskHandle;
extern osThreadId_t GUITaskHandle;
extern osThreadId_t ControlTaskHandle;
extern osThreadId_t RxTaskHandle;
extern osThreadId_t BeeperTaskHandle;

extern osMessageQueueId_t uartRxQueueHandle;
extern osMessageQueueId_t beepQueueHandle;
extern osMutexId_t UartTxMutexHandle;
extern osSemaphoreId_t BinarySem_UartRxHandle;

// --- 全局变量 (保持原名) ---
extern uint16_t adc_dma_buffer[5];
extern uint8_t status[24];
extern uint8_t rx_buffer[255];
extern uint8_t isr_rx_buffer[255];
extern volatile uint16_t isr_rx_len;
extern volatile uint32_t restart_cnt;
extern volatile uint32_t last_receive_timestamp;
extern volatile uint8_t communication_connected;
extern volatile uint16_t last_dma_pos;
extern uint8_t grass_bag_status;
extern volatile uint8_t TEST_OK;
extern volatile uint16_t new_address;
// 调试变量
extern volatile uint16_t g_dbg_lx, g_dbg_ly, g_dbg_rx, g_dbg_ry;
extern volatile uint8_t g_dbg_keys[11];
extern volatile uint32_t g_tx_ok_cnt;
extern volatile uint32_t g_tx_fail_cnt;

// 遥控器安全锁标志位
extern volatile bool g_is_unlocked;

extern char g_current_pin[5];
// 系统监控
typedef struct
{
  uint32_t Control_FreeStack;
  uint32_t GUI_FreeStack;
  uint32_t Default_FreeStack;
  uint32_t FreeHeapSize;
  uint32_t MinEverFreeHeap;
} SystemMonitor_t;
extern SystemMonitor_t g_SysMonitor;

// 发送缓冲区 (原 static，现改为全局以便 Control 和 Comm 共享)
extern uint8_t tx_joy_buf[10];
extern uint8_t tx_key_buf[15];

typedef struct
{
  // --- 安全功能开关 (1: 开启, 0: 关闭) ---
  uint8_t ultrasonic_en;    // 超声波雷达使能
  uint8_t collision_en;     // 碰撞传感器使能
  uint8_t reverse_limit_en; // 后退限速使能
  uint8_t tilt_sensor_en;   // 倾斜传感器使能

  uint8_t lawn_protect_en; // 1: 开启护草模式 (禁止原地转弯), 0: 关闭
  // API 上位机控制授权使能
  uint8_t api_enable;

  //--- 其他功能配置 ---
  uint8_t blade_init_en; // 初始化刀盘使能
  // uint8_t  ultrasonic_sensitivity; // 雷达灵敏度 (0:低, 1:中, 2:高)
  // 操作模式设置（0：双手1；1：双手2；3：单手1；4：单手2）
  uint8_t op_mode;

  uint8_t cruise_ctrl_req; // 开启定速巡航请求 (单次触发)
} SystemConfig_t;

// 声明外部全局变量
extern SystemConfig_t g_sys_config;

extern bool g_hide_cruise_popup_this_session;

// 增加 UI 触发配对的标志位
extern volatile bool g_trigger_pairing_from_ui;
#endif
