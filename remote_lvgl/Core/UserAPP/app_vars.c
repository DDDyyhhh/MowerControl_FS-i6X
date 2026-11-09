/**
  ******************************************************************************
  * @file           : app_vars.c
  * @author         : [Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : 遥控器端全局共享变量内存分配
  * 
  * 主要功能：
  * 1. 集中定义了 DMA 接收缓冲区、解析长度变量
  * 2. 定义了用于 UI 和系统调试的全局变量 (`g_dbg_lx`, `restart_cnt` 等)
  * 3. 存放摇杆与按键的发送组包缓冲区 (`tx_joy_buf`, `tx_key_buf`)
  *
  * 注意事项：
  * - 避免重复定义，若已在 `main.c` 中定义，此处仅使用 `extern` 声明即可。
  ******************************************************************************
  */

#include "app_vars.h"

// 这里定义那些原本在 freertos.c 里定义的变量
// 注意：status, adc_dma_buffer 等如果在 main.c 定义了，这里就不要定义，只在 .h extern 即可

uint8_t isr_rx_buffer[255];
volatile uint16_t isr_rx_len = 0;
volatile uint32_t restart_cnt = 0;

volatile uint16_t g_dbg_lx, g_dbg_ly, g_dbg_rx, g_dbg_ry;
volatile uint8_t g_dbg_keys[11];
volatile uint32_t g_tx_ok_cnt = 0;
volatile uint32_t g_tx_fail_cnt = 0;
volatile bool g_is_unlocked = false;

char g_current_pin[5] = "1234"; // 系统出厂默认密码
SystemMonitor_t g_SysMonitor;

uint8_t tx_joy_buf[10];
uint8_t tx_key_buf[15];

// 实例化全局配置，并赋初始值（出于安全考虑，开机默认全开防撞和限速）
SystemConfig_t g_sys_config = {
    .ultrasonic_en = 1,
    .collision_en = 1,
    .reverse_limit_en = 1,
    .tilt_sensor_en = 1,
    .lawn_protect_en = 0,

    .blade_init_en = 0, // 初始化刀盘使能
    .op_mode = 0, // 默认操作模式（双手1）
    .cruise_ctrl_req = 0 // 默认不请求巡航控制
};

volatile bool g_trigger_pairing_from_ui = false;

