/**
 ******************************************************************************
 * @file           : app_control.c
 * @author         : [Your Name/Team]
 * @version        : V1.2
 * @date           : 2026-03-11
 * @brief          : 遥控器摇杆/按键采集与上行链路 (TX) 发送任务
 *
 * @note           任务优先级 : 高 (High)
 *
 * 主要功能：
 * 1. 周期性 (10ms) 轮询非阻塞按键状态机 `KEY_Scan`
 * 2. 直接读取 DMA 搬运的 ADC 摇杆数据
 * 3. 错峰发送策略 (Time-Slicing)：每 50ms 摇杆与按键交替发送，防止无线模块溢出
 *
 * 注意事项：
 * - 长按 Key6 触发配对模式 (`Remote_Pairing_Process_Blocking`) 会短时间阻塞此任务。
 * - 兼容旧版与新版拨杆操作模式 (`KEY_MODE` 宏切换)。
 ******************************************************************************
 */
#include "app_control.h"
#include "app_vars.h"
#include "app_comm.h" // 需要调用发送函数
#include "key.h"
#include "pairing_remote.h"
#include "beep.h"
#include "connet_check.h"
void Send_Joystick_Data(uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry);
void Send_Key_Events(void);
uint8_t Get_Drive_Speed(void);
uint8_t Get_Cutter_Speed(void);
uint8_t Get_Height_Level(void);
#define KEY_MODE 1 // 0=旧版遥控器，1=新版遥控器模式
/* USER CODE BEGIN Header_StartControlTask */
/**
 * @brief Function implementing the ControlTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartControlTask */
void App_Control_Main(void)
{
  /* USER CODE BEGIN StartControlTask */

  uint32_t last_send_tick = 0;

  // 交替标志位：0发摇杆，1发按键
  static uint8_t tx_slot = 0;
  // 摇杆历史值
  uint16_t last_lx = 0, last_ly = 0, last_rx = 0, last_ry = 0;
  /* Infinite loop */
  for (;;)
  {

    // 1. 扫描按键 (每10ms一次)
    KEY_Scan(0); // 注意：修改 KEY_Scan 内部，不要使用 HAL_Delay，或者传入0
    // Send_Key_Events(); // 处理按键事件

    // 【全局事件缓存与调试同步】
    // 每一轮循环中，只读取一次按键事件并缓存起来。
    // 这完美解决了 key_event 读取即清除（Clear on read）导致的多处重复读取失效问题！
    for (uint8_t i = 0; i < 8; i++)
    {
      g_dbg_keys[i] = key_event(i + 1); // 实时刷新调试面板数值，不受解锁状态影响！
    }

    uint8_t key_pack = 0;

    // 2. 摇杆数据处理 (ADC数据由DMA自动更新，直接读即可)
    uint16_t lx = adc_dma_buffer[1];
    uint16_t ly = adc_dma_buffer[0];
    uint16_t rx = adc_dma_buffer[2];
    uint16_t ry = adc_dma_buffer[3];

    // ============================================================
    // <<<--- 【安全锁拦截】 --->>>
    // 如果没有解锁，强制将所有摇杆数据归中，按键状态清零！
    // ============================================================
    if (g_is_unlocked == false)
    {
      lx = 2048;
      ly = 2048;
      rx = 2048;
      ry = 2048;
      // 注意：这里不用强行 return，我们依然让系统保持通信心跳，
      // 只是发送出去的是绝对安全的 0 动力指令。
    }

    // --- 调试：更新全局变量 ---
    g_dbg_lx = lx;
    g_dbg_ly = ly;
    g_dbg_rx = rx;
    g_dbg_ry = ry;

    // 3. 发送逻辑
    // 策略：每 50ms 发送一次摇杆数据 (E34 模块建议不要太快)
    // 按键如果有变化，立即发送 (或者也放在定时里)

    // 2. 发送逻辑 (错峰发送)
    // 将间隔改为 50ms
    if (HAL_GetTick() - last_send_tick > 50)
    {
      if (tx_slot == 0)
      {
        // --- 时间片 0：发送摇杆 ---
        Send_Joystick_Data(lx, ly, rx, ry);
        tx_slot = 1; // 下次发按键
      }
      else
      {
        // --- 时间片 1：发送按键 ---
        // 此时距离摇杆发送已经过了 50ms，串口肯定空闲了

        // 同理，如果未解锁，不应该发送刀盘开启等按键命令
        if (g_is_unlocked == true)
        {
          Send_Key_Events();
        }

        tx_slot = 0; // 下次发摇杆
      }

      last_send_tick = HAL_GetTick();
    }

    // ============================================================
    // <<<---  监测物理按键 或 UI 点击信号 --->>>
    // ============================================================
    if (g_trigger_pairing_from_ui == true)
    {
      // 1. 立即清除 UI 触发标志，防止重复进入
      g_trigger_pairing_from_ui = false;

      // 2. 执行阻塞配对逻辑
      // 注意：此函数内部有 osDelay，会释放 CPU，所以不会影响 GUI 任务刷新屏幕
      Remote_Pairing_Process_Blocking();
    }
    osDelay(10); // 适当延长延时，降低 CPU 占用率
  }

  /* USER CODE END StartControlTask */
}

// ... Send_Joystick_Data 和 Send_Key_Events 的实现 ...

void Send_Joystick_Data(uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry)
{
  tx_joy_buf[0] = 0xAB;
  tx_joy_buf[1] = (lx >> 8) & 0xFF;
  tx_joy_buf[2] = lx & 0xFF;
  tx_joy_buf[3] = (ly >> 8) & 0xFF;
  tx_joy_buf[4] = ly & 0xFF;
  tx_joy_buf[5] = (rx >> 8) & 0xFF;
  tx_joy_buf[6] = rx & 0xFF;
  tx_joy_buf[7] = (ry >> 8) & 0xFF;
  tx_joy_buf[8] = ry & 0xFF;
  tx_joy_buf[9] = 0xBA;

  Safe_UART_DMA_Send(tx_joy_buf, 10);
}

#if (KEY_MODE == 0)
// 按键发送封装
void Send_Key_Events(void)
{
  tx_key_buf[0] = 0xCC;

  for (uint8_t i = 0; i < 8; i++)
  {
    // 1. 读取按键状态 (这是唯一的读取点！)
    uint8_t k_val = key_event(i + 1) & 0x03;

    // 2. 存入发送缓冲区
    tx_key_buf[1 + i] = k_val;

    // 3. 【关键】同步更新到全局调试数组，供 GUI 查看
    g_dbg_keys[i] = k_val;
  }

  tx_key_buf[9] = 0;
  tx_key_buf[10] = grass_bag_status;
  tx_key_buf[11] = 0xDD;

  Safe_UART_DMA_Send(tx_key_buf, 12);
}
#elif (KEY_MODE == 1)
// 按键发送封装
void Send_Key_Events(void)
{
  tx_key_buf[0] = 0xCC;

  for (uint8_t i = 0; i < 8; i++)
  {
    // 直接读取我们已经缓存并过滤后的全局调试按键数组！
    uint8_t k_val = g_dbg_keys[i] & 0x03;
    tx_key_buf[1 + i] = k_val;
  }
  // 读取并写入挡位信息
  tx_key_buf[9] = Get_Drive_Speed();   // 驱动速度挡位 (0-3)
  g_dbg_keys[8] = tx_key_buf[9];       // 同步更新调试数组
  tx_key_buf[10] = Get_Cutter_Speed(); // 割刀速度挡位 (0-3)
  g_dbg_keys[9] = tx_key_buf[10];      // 同步更新调试数组
  tx_key_buf[11] = Get_Height_Level(); // 高度挡位 (0-5)
  g_dbg_keys[10] = tx_key_buf[11];     // 同步更新调试数组

  // ============================================================
  // <<<---  打包触摸屏系统配置 (Bit-packing) --->>>
  // ============================================================
  uint8_t config_flags = 0;

  // 利用位操作，将 5 个开关压缩进 1 个字节
  if (g_sys_config.ultrasonic_en)
    config_flags |= (1 << 0); // Bit 0
  if (g_sys_config.collision_en)
    config_flags |= (1 << 1); // Bit 1
  if (g_sys_config.reverse_limit_en)
    config_flags |= (1 << 2); // Bit 2
  if (g_sys_config.tilt_sensor_en)
    config_flags |= (1 << 3); // Bit 3
  if (g_sys_config.blade_init_en)
    config_flags |= (1 << 4); // Bit 4
  if (g_sys_config.cruise_ctrl_req)
    config_flags |= (1 << 5); // Bit 5
  if (g_sys_config.lawn_protect_en)
    config_flags |= (1 << 6); // Bit 6
  tx_key_buf[12] = config_flags;         // 第 12 字节：安全开关集合
  tx_key_buf[13] = g_sys_config.op_mode; // 第 13 字节：操作模式 (0~3)

  // 刀盘初始化是一个“单次触发”指令。
  // 发送完这一次后，必须立刻将其清零，否则控制板会一直收到初始化指令！
  g_sys_config.blade_init_en = 0;
  g_sys_config.cruise_ctrl_req = 0; // 清零巡航请求
  // ============================================================

  tx_key_buf[14] = 0xDD;

  Safe_UART_DMA_Send(tx_key_buf, 15);
}
/**
 * @brief 读取驱动速度挡位（PE8, PE9, PE10）
 * @return 0=无档, 1=档1, 2=档2, 3=档3
 */

uint8_t Get_Drive_Speed(void)
{
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_8) == GPIO_PIN_RESET)
    return 1;
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9) == GPIO_PIN_RESET)
    return 2;
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_10) == GPIO_PIN_RESET)
    return 3;
  return 0;
}

/**
 * @brief 读取割刀速度挡位（PE11, PE12, PE13）
 * @return 0=无档, 1=档1, 2=档2, 3=档3
 */
uint8_t Get_Cutter_Speed(void)
{
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_11) == GPIO_PIN_RESET)
    return 1;
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_12) == GPIO_PIN_RESET)
    return 2;
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_13) == GPIO_PIN_RESET)
    return 3;
  return 0;
}

/**
 * @brief 读取高度档位（PB9, PB7, PB6, PB1, PB0）
 * @return 0=无档, 1..5=对应档位
 */
uint8_t Get_Height_Level(void)
{
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
    return 1;
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET)
    return 2;
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET)
    return 3;
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET)
    return 4;
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET)
    return 5;
  return 0;
}

#endif