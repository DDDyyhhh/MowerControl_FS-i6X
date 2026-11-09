/**
 ******************************************************************************
 * @file           : key.c
 * @author         : [Your Name/Team]
 * @version        : V1.1
 * @date           : 2026-03-11
 * @brief          : 多通道非阻塞按键状态机 (Debounce & Long Press)
 *
 * 主要功能：
 * 1. 基于 4 个独立状态 (空闲、消抖、按下、长按) 追踪 8 个按键的物理事件
 * 2. 精确区分并输出 按下 (`KEY_EVENT 1`) 与 长按 (`KEY_EVENT 2`)
 *
 * 注意事项：
 * - 纯轮询算法，无任何 `HAL_Delay`，完美适配 RTOS 周期性 Task。
 * - 硬件映射采用结构体数组查表，易于后续扩展或修改引脚。
 ******************************************************************************
 */
#include "key.h"
#include "main.h"
#include "stdio.h"
#include "usart.h"

// 全局按键状态机数组
static KeyFSM key_fsm[8] = {0};

/**
 * @brief 按键状态机扫描函数（需要在主循环中周期性调用）
 * @param interval_ms 扫描间隔时间(ms)
 */
void KEY_Scan(uint32_t interval_ms)
{
    static uint32_t last_scan_time = 0;
    uint32_t current_time = HAL_GetTick();

    // 时间间隔检查
    if (current_time - last_scan_time < interval_ms)
    {
        return;
    }
    last_scan_time = current_time;

    for (int i = 0; i < 8; i++)
    {
        uint8_t current_level = KEY_Read(i + 1); // 读取当前电平

        switch (key_fsm[i].state)
        {
        case 0: // 空闲状态
            if (current_level == 1 && key_fsm[i].last_level == 0)
            {
                // 检测到下降沿，进入消抖状态
                key_fsm[i].state = 1;
                key_fsm[i].press_time = current_time;
                key_fsm[i].event = 0; // 清除之前的事件
            }
            break;

        case 1: // 消抖状态
            if (current_level == 1)
            {
                // 消抖时间到，确认按下
                if (current_time - key_fsm[i].press_time >= KEY_DEBOUNCE_TIME)
                {
                    key_fsm[i].state = 2; // 进入按下状态
                    key_fsm[i].event = 1; // 按下就输出1
                }
            }
            else
            {
                // 抖动，回到空闲状态
                key_fsm[i].state = 0;
                key_fsm[i].event = 0;
            }
            break;

        case 2: // 按下状态
            if (current_level == 0)
            {
                // 按键释放，回到空闲状态
                key_fsm[i].state = 0;
                key_fsm[i].event = 0;
            }
            else
            {
                // 仍然按下，检查是否达到长按时间
                if (current_time - key_fsm[i].press_time >= KEY_LONG_PRESS_TIME)
                {
                    key_fsm[i].state = 3; // 进入长按状态
                    key_fsm[i].event = 2; // 超时就输出2
                }
                else
                {
                    // 在按下但未超时期间持续输出1
                    key_fsm[i].event = 1;
                }
            }
            break;

        case 3: // 长按状态
            if (current_level == 0)
            {
                // 长按释放，回到空闲状态
                key_fsm[i].state = 0;
                key_fsm[i].event = 0;
            }
            else
            {
                // 长按状态下持续输出2
                key_fsm[i].event = 2;
            }
            break;
        }

        key_fsm[i].last_level = current_level;
    }
}

/**
 * @brief 获取按键事件
 * @param key_num 按键编号(1-8)
 * @return 按键事件类型
 */
KeyEvent KEY_GetEvent(uint8_t key_num)
{
    if (key_num < 1 || key_num > 8)
    {
        return KEY_EVENT_NONE;
    }

    KeyFSM *key = &key_fsm[key_num - 1];
    KeyEvent event = key->event;

    // 读取后清除事件（长按事件在长按状态下会持续产生）
    if (key->state != 3)
    {
        key->event = KEY_EVENT_NONE;
    }

    return event;
}

/**
 * @brief 检查特定按键事件（您期望的调用方式）
 * @param key_num 按键编号(1-8)
 * @return 按键事件类型
 */
KeyEvent key_event(uint8_t key_num)
{
    return KEY_GetEvent(key_num);
}

/**
 * @brief 按键初始化（使用您原有的初始化函数）
 */
void KEY_Init(void)
{
    // GPIO_InitTypeDef GPIO_InitStruct = {0};

    // // 使能GPIO时钟
    // __HAL_RCC_GPIOA_CLK_ENABLE();
    // __HAL_RCC_GPIOD_CLK_ENABLE();

    // // PA3
    // GPIO_InitStruct.Pin = GPIO_PIN_3;
    // GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    // HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // // PD2, PD3, PD4, PD7, PD8, PD14, PD15
    // GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_14 | GPIO_PIN_15;
    // GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    // HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // 初始化状态机
    for (int i = 0; i < 8; i++)
    {
        key_fsm[i].state = 0;
        key_fsm[i].event = KEY_EVENT_NONE;
        key_fsm[i].last_level = KEY_Read(i + 1);
    }
}

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} KeyMap;

// {GPIOA, GPIO_PIN_3},
//     {GPIOD, GPIO_PIN_2},
//     {GPIOD, GPIO_PIN_14},
//     {GPIOD, GPIO_PIN_4},
//     {GPIOD, GPIO_PIN_3},
//     {GPIOD, GPIO_PIN_15},
//     {GPIOD, GPIO_PIN_7},
//     {GPIOD, GPIO_PIN_8}

static const KeyMap key_map[8] = {
    {GPIOA, GPIO_PIN_3},
    {GPIOD, GPIO_PIN_2},
    {GPIOD, GPIO_PIN_3},
    {GPIOD, GPIO_PIN_4},
    {GPIOD, GPIO_PIN_14},
    {GPIOD, GPIO_PIN_8},
    {GPIOD, GPIO_PIN_7},
    {GPIOD, GPIO_PIN_15}};

uint8_t KEY_Read(uint8_t key_num)
{
    if (key_num < 1 || key_num > 8)
        return 0;
    return HAL_GPIO_ReadPin(key_map[key_num - 1].port, key_map[key_num - 1].pin) == GPIO_PIN_RESET ? 1 : 0;
}

// void Send_Key_Status(void)
// {

//     uint8_t buf[3];
//     buf[0] = 0xCC; // 帧头

//     // 8个按键状态打包到一个字节
//     buf[1] = 0;
//     for (uint8_t i = 0; i < 8; i++) {
//         if (KEY_Read(i + 1)) buf[1] |= (1 << i);
//     }

//     buf[2] = 0xDD; // 帧尾

//     HAL_UART_Transmit(&huart3, buf, 3, HAL_MAX_DELAY);

// }
extern uint8_t Target_height;
extern uint8_t grass_bag_status;

// void Send_Key_Events(void)
// {
//     static uint8_t key_buf[12];

//     key_buf[0] = 0xCC; // 帧头

//     // 2. 检查串口是否忙 (上一包发完了没？)
//     // 如果正在发，这次就丢弃，或者等待

//     key_buf[0] = 0xCC; // 帧头

//     // 8个按键事件
//     for (uint8_t i = 0; i < 8; i++) {
//         key_buf[1 + i] = key_event(i + 1) & 0x03;
//     }

//     // 状态信息
//     key_buf[9] = 0;
//     key_buf[10] = grass_bag_status;
//     key_buf[11] = 0xDD; // 帧尾

//     // 3. 使用 DMA 发送 (非阻塞)
//     HAL_UART_Transmit_DMA(&huart3, key_buf, 12);
// }
