/**
  ******************************************************************************
  * @file           : pairing_remote.c
  * @author         : [Your Name/Team]
  * @version        : V1.1
  * @date           : 2026-03-11
  * @brief          : 遥控器端动态配对与无线模块适配层
  * 
  * 主要功能：
  * 1. 提供无线模块 (A28/E34) 的统一抽象控制接口
  * 2. 执行三阶段握手配对机制 (发送 REQ -> 发送新 ADDR -> 测试新信道)
  * 3. 包含超时防卡死与自动重启配对状态机逻辑 (`RESTART_PAIRING`)
  *
  * 注意事项：
  * - 配对过程是阻塞式的 (Blocking)，调用期间将挂起发送任务，通过 osDelay 让出 CPU 给接收解析任务。
  ******************************************************************************
  */

#include "pairing_remote.h"
#include "app_vars.h"
#include "app_comm.h" 
#include "beep.h"     
#include "cmsis_os.h"
#include <stdlib.h>
#include <string.h>
#include "flash_storage.h"
#include "app_gui.h"
#include "connet_check.h"

// ============================================================
// <<<--- 【硬件平台切换宏】 无线通信模块选择 --->>>
// ============================================================
// 0: 使用老款 E34 模块 (半双工)
// 1: 使用新款 A28 模块 (全双工，推荐)
#define WIRELESS_MODULE_TYPE 1 

#if WIRELESS_MODULE_TYPE == 0
    #include "E34.h"
#elif WIRELESS_MODULE_TYPE == 1
    #include "A28.h"
#else
    #error "Please select a valid WIRELESS_MODULE_TYPE (0 or 1)"
#endif
// ============================================================

// --- 全局变量 ---
volatile uint16_t new_address = 0;

// --- 内部标志位 (由 Rx 回调修改) ---
static volatile uint8_t flag_ACK = 0;
static volatile uint8_t flag_CONFIRM = 0;
static volatile uint8_t flag_TEST_OK = 0;

volatile bool g_is_pairing_mode = false;


// ============================================================
// <<<--- 抽象适配函数 --->>>
// ============================================================
static void RF_Module_Set_Address(uint16_t addr)
{
#if WIRELESS_MODULE_TYPE == 0
    E34_Set_Address(addr);
#elif WIRELESS_MODULE_TYPE == 1
    A28_Set_Address(addr);
#endif
}


// --- 辅助发送函数 ---
static void Send_Cmd(uint8_t cmd, uint8_t *payload, uint8_t len)
{
    uint8_t buf[10];
    uint8_t idx = 0;
    buf[idx++] = 0xAA;
    buf[idx++] = cmd;
    if (len > 0 && payload != NULL)
    {
        memcpy(&buf[idx], payload, len);
        idx += len;
    }
    buf[idx++] = 0xBB;

    // 使用互斥锁保护的 DMA 发送，防止与摇杆数据冲突
    Safe_UART_DMA_Send(buf, idx);
}

// --- 消息处理 (RxTask 调用) ---
void Pairing_Remote_Handle_Message(uint8_t *data, uint16_t length)
{
    if (length < 2)
        return;

    uint8_t cmd = data[1]; // 命令字

    switch (cmd)
    {
    case PAIRING_ACK:
        flag_ACK = 1;
        break;
    case PAIRING_CONFIRM:
        flag_CONFIRM = 1;
        break;
    case PAIRING_TEST_OK:
        flag_TEST_OK = 1;
        break;
    }
}

// --- 主配对流程 (ControlTask 调用) ---
void Remote_Pairing_Process_Blocking(void)
{
    int timeout_counter = 0;
    g_is_pairing_mode = true; // 标记为配对中

// 【新增】定义一个重启点
RESTART_PAIRING:
    // 1. 初始化
    flag_ACK = 0;
    flag_CONFIRM = 0;
    flag_TEST_OK = 0;

    // 生成随机地址 (1 ~ 65534)
    srand(HAL_GetTick());
    uint16_t temp_addr = (rand() % 0xFFFE) + 1;

    // 提示音
    Play_Beep(BEEP_SHORT);

    // 2. 恢复默认地址 (0x0000)
    // 【修改】调用抽象接口
    RF_Module_Set_Address(DEFAULT_ADDRESS);
    osDelay(50); // 额外给一点时间让模块稳定

    // ============================================================
    // 阶段 1: 发送 REQ -> 等待 ACK (无限等待)
    // ============================================================
    timeout_counter = 0;
    while (flag_ACK == 0)
    {
        // 发送请求
        Send_Cmd(PAIRING_REQ, NULL, 0);

        // 必须使用 osDelay 让出 CPU 给 RxTask
        osDelay(50);
        
        // 超时机制 (例如 5秒)
        timeout_counter++;
        if (timeout_counter > 20) // 20 * 50ms = 1s
        { 
            // 如果一直没收到 ACK，说明控制板没开配对，重新尝试
            Play_Beep(BEEP_ALARM_FAULT);
            goto RESTART_PAIRING; 
        }
    }

    // ============================================================
    // 阶段 2: 发送 ADDR -> 等待 CONFIRM (无限等待)
    // ============================================================
    uint8_t addr_buf[2];
    addr_buf[0] = (temp_addr >> 8) & 0xFF;
    addr_buf[1] = temp_addr & 0xFF;

    while (flag_CONFIRM == 0)
    {
        Send_Cmd(PAIRING_ADDR, addr_buf, 2);
        osDelay(50);

        // 超时机制
        timeout_counter++;
        if (timeout_counter > 20)
        { 
            goto RESTART_PAIRING; 
        }
    }

    // ============================================================
    // 阶段 3: 切换自身地址 -> 发送 TEST -> 等待 OK (无限等待)
    // ============================================================
    // 此时接收端已经收到地址并回复 CONFIRM，它正在切换地址
    // 我们也切换到新地址
    // 【修改】调用抽象接口
    RF_Module_Set_Address(temp_addr);
    osDelay(50); // 等待模块生效

    while (flag_TEST_OK == 0)
    {
        Send_Cmd(PAIRING_TEST, NULL, 0);
        osDelay(50);

        // 超时机制
        timeout_counter++;
        if (timeout_counter > 20)
        {                         
            goto RESTART_PAIRING; 
        }
    }

    // ============================================================
    // 成功
    // ============================================================
    // 此时 GUI 任务检测到 new_address 变化，才会刷新屏幕左下角
    new_address = temp_addr;
    
    // 如果你在遥控器端也要做断电保存，在这里调用 Flash 写入
    // Flash_Save_Address(new_address);
    
    Play_Beep(BEEP_CONNECT);
    g_is_pairing_mode = false; // 标记为结束

    // 此时 new_address 已经生效，且模块已配置为新地址
}