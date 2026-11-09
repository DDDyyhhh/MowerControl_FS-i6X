/**
  ******************************************************************************
  * @file           : A28.c
  * @author         : [Your Name/Team]
  * @version        : V1.2
  * @date           : 2026-03-11
  * @brief          : 泽耀 A28-T2G4A12D1a 全双工无线模块底层驱动
  * 
  * 主要功能：
  * 1. 提供 MD0/MD1 硬件引脚控制，实现休眠配置、半双工、全双工模式切换
  * 2. 利用影子寄存器 (Shadow Registers) 机制，支持单独修改地址、信道或功率
  * 3. 支持实时查询通信 RSSI 与环境底噪 RSSI
  * 
  * 注意事项：
  * - 手册规定 MD0=0, MD1=1 才是全双工模式。
  * - 默认波特率已改为 9600 ，请确保 huart3 也配置为 9600，否则会因波特率不匹配而失联。
  ******************************************************************************
  */
#include "A28.h"
#include "cmsis_os.h" // 强烈建议使用 RTOS 的 osDelay

// ==========================================
// 影子配置寄存器 (Shadow Configuration)
// ==========================================
// 数组格式: [0]指令码, [1]ADDH, [2]ADDL, [3]SPEED, [4]CHAN, [5]OPTION
// 默认配置: 地址 0x0000, 波特率 115200(8N1), 空速0, 信道 0, FLRC调制, 12dBm功率
static uint8_t a28_config[6] = {
    0xC0,                   // 0xC0: 配置掉电保存
    0x00,                   // ADDH: 地址高位
    0x00,                   // ADDL: 地址低位
    A28_BAUD_115200 | A28_AIR_SPEED_3, // SPEED: 115200, 空速3
    0x00,                   // CHAN: 信道 0
    A28_MOD_FLRC | A28_POWER_12DBM     // OPTION: FLRC模式, 12dBm
};

/**
 * @brief 初始化 M0/M1 控制引脚
 */
void A28_Mode_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}

/**
 * @brief 进入休眠配置模式 (MD0=1, MD1=1)
 */
void A28_Set_Config_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET); 
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET); 
    osDelay(20); // 必须等待状态切换完成
}

/**
 * @brief 进入半双工透传模式 (MD0=0, MD1=0)
 */
void A28_Set_HalfDuplex_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET); 
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_RESET); 
    osDelay(20);
}

/**
 * @brief 进入全双工透传模式 (MD0=0, MD1=1) - 【强烈推荐此模式】
 */
void A28_Set_FullDuplex_Mode(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET); 
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET); 
    osDelay(20);
}

/**
 * @brief 将影子寄存器的配置一次性写入模块
 */
void A28_Apply_Config(void)
{
    // 1. 切入配置模式
    A28_Set_Config_Mode();
    
    // 2. 发送 6 字节配置指令
    HAL_UART_Transmit(&huart3, a28_config, 6, 100);
    osDelay(50); // 等待模块内部 Flash 写入完成
    
    // 3. 读取配置以确认 (可选，这里仅触发读取，不处理应答)
    uint8_t read_cmd[3] = {0xC1, 0xC1, 0xC1};
    HAL_UART_Transmit(&huart3, read_cmd, 3, 100);
    osDelay(50);
    
    // 4. 切回全双工工作模式
    A28_Set_FullDuplex_Mode(); 
}

/**
 * @brief 动态设置模块地址 (0x0000~0xFFFF)
 */
void A28_Set_Address(uint16_t addr)
{
    a28_config[1] = (addr >> 8) & 0xFF; // 高8位
    a28_config[2] = addr & 0xFF;        // 低8位
    A28_Apply_Config();                 // 统一写入
}

/**
 * @brief 动态设置模块通信信道
 * @param channel 0~100(FLRC), 0~50(LORA)
 */
void A28_Set_Channel(uint8_t channel)
{
    a28_config[4] = channel;
    A28_Apply_Config();
}

/**
 * @brief 设置调制方式与功率
 * @param power A28_POWER_xxx 宏
 * @param modulation A28_MOD_FLRC 或 A28_MOD_LORA
 */
void A28_Set_Power_And_Modulation(uint8_t power, uint8_t modulation)
{
    a28_config[5] = power | modulation;
    A28_Apply_Config();
}

/**
 * @brief 读取当前接收数据的 RSSI
 * @return 负值表示真实 RSSI (如 -85)，返回 0 或正值表示读取失败
 */
int16_t A28_Read_Current_RSSI(void)
{
    uint8_t read_cmd[6] = {0xAF, 0xAF, 0x73, 0x00, 0xAF, 0xF3};
    uint8_t response[1] = {0};
    
    // 清空接收缓存
    __HAL_UART_FLUSH_DRREGISTER(&huart3);
    
    HAL_UART_Transmit(&huart3, read_cmd, 6, 50);
    
    if (HAL_UART_Receive(&huart3, response, 1, 100) == HAL_OK) {
        // 模块返回的是绝对值，真实 RSSI 是负数
        return -((int16_t)response[0]); 
    } 
    return 0; // 错误
}

/**
 * @brief 读取当前环境底噪 RSSI
 */
int16_t A28_Read_Environment_RSSI(void)
{
    uint8_t read_cmd[6] = {0xAF, 0xAF, 0x74, 0x00, 0xAF, 0xF4};
    uint8_t response[1] = {0};
    
    __HAL_UART_FLUSH_DRREGISTER(&huart3);
    
    HAL_UART_Transmit(&huart3, read_cmd, 6, 50);
    
    if (HAL_UART_Receive(&huart3, response, 1, 100) == HAL_OK) {
        return -((int16_t)response[0]); 
    } 
    return 0; // 错误
}