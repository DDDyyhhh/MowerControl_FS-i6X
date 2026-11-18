/**
  ******************************************************************************
  * @file           : AXEBMS_RS485.c
  * @brief          : AXE 电池管理系统 (BMS) 的 RS485 / Modbus RTU 协议驱动
  * @author         : [Your Name/Team]
  * @version        : V1.3.4
  * @date           : 2026-03-11
  * 主要功能：
  * 1. 注册 `BMS_ModbusCallback` 异步回调函数，解析 BMS 返回的 32 字节核心数据
  *    (包括总电压、电流、6路温度、最高/最低单体电压、相对容量 RSOC 等)
  * 2. 定时 (500ms) 通过 Modbus 总线发送读保持寄存器 (0x03) 指令
  * 3. 实现了欠压预警 (Warming) 与 欠压保护 (Protect) 的时间防抖滤波逻辑
  * 4. 提供了标准的 CRC16 校验算法实现
  *
  * 注意事项：
  * - 强依赖外部全局变量 `my_bms` 的状态，回调函数直接修改该内存区域
  * - `AXEBMS_RS485_Process` 必须被高频调用，以便驱动 `ModbusRtuBus_Process` 的超时机制
  ******************************************************************************
  */

#include "AXEBMS_RS485.h"
#include "main.h"
#include "rs485.h"
#include "modbus_rtu_bus.h" // <<<--- 关键：包含新的总线头文件
#include <string.h>     // 包含 memcpy
#define AXEBMS_RS485_TICK_MS      500
#define AXEBMS_RS485_DELAY_1S     1000
#define MODBUS_READ_HOLDING_REGISTERS  0x03
// 全局变量，回调函数需要通过它找到BMS实例
// 确保在 main.c 中定义: AXEBMS_RS485 my_bms;
extern AXEBMS_RS485 my_bms; 
static uint32_t lastProcessTime = 0;


/**************************************************************
 * 新增: Modbus响应回调函数
 **************************************************************/
static void BMS_ModbusCallback(uint8_t* pdu, uint16_t pdu_len) {
    // pdu: 指向Modbus响应的数据部分 (已去除地址、功能码、长度字节和CRC)
    // pdu_len: 数据部分的长度
    
    if (pdu != NULL && pdu_len == 32) {
        my_bms.sample_sequence++;
        __DMB();
        my_bms.data.Voltage = (pdu[0] << 8) | pdu[1];
        my_bms.data.CurCadc = ((int16_t)((pdu[2] << 8) | pdu[3])) - 10000;
        my_bms.data.Temp1 = (pdu[4] << 8) | pdu[5];
        my_bms.data.Temp2 = (pdu[6] << 8) | pdu[7];
        my_bms.data.Temp3 = (pdu[8] << 8) | pdu[9];
        my_bms.data.Temp4 = (pdu[10] << 8) | pdu[11];
        my_bms.data.Temp5 = (pdu[12] << 8) | pdu[13];
        my_bms.data.Temp6 = (pdu[14] << 8) | pdu[15];
        my_bms.data.Tmax = (pdu[16] << 8) | pdu[17];
        my_bms.data.Tmin = (pdu[18] << 8) | pdu[19];
        my_bms.data.Vmax = (pdu[20] << 8) | pdu[21];
        my_bms.data.Vmin = (pdu[22] << 8) | pdu[23];
        my_bms.data.VmaxminNo = (pdu[24] << 8) | pdu[25];
        my_bms.data.RSOC = (pdu[26] << 8) | pdu[27];
        my_bms.data.FCC = (pdu[28] << 8) | pdu[29];
        my_bms.data.RC = (pdu[30] << 8) | pdu[31];
        my_bms.last_good_tick = HAL_GetTick();
        my_bms.sample_valid = true;
        my_bms.validCnt = 1;
        __DMB();
        my_bms.sample_sequence++;
    }
}



void AXEBMS_RS485_Init(AXEBMS_RS485* bms, uint8_t devAddr) {
    bms->sample_valid = false;
    bms->last_good_tick = 0U;
    bms->sample_sequence = 0U;
    bms->devAddr = devAddr;
    
    bms->warmingVoltage = 0;
    bms->protectVoltage = 0;
    bms->warmingVoltageDelayMs = AXEBMS_RS485_DELAY_1S;
    bms->protectVoltageDelayMs = AXEBMS_RS485_DELAY_1S;
    
    bms->state = 0;
    bms->validCnt = 0;
    
    bms->warmingVoltageTimer = 0;
    bms->protectVoltageTimer = 0;
    bms->warmingVoltageCount = 0;
    bms->protectVoltageCount = 0;
    
    bms->kVoltageWarming = false;
    bms->kVoltageProtected = false;

    bms->rxDataLength = 0;
    
    // 初始化数据
    memset(&bms->data, 0, sizeof(AXEBMS_Data));
    // <<<--- 关键：初始化Modbus总线，并把我们的回调函数注册进去
    ModbusRtuBus_Init(BMS_ModbusCallback);
}

void AXEBMS_RS485_Start(AXEBMS_RS485* bms) {
    // 可以在这里添加启动逻辑
    lastProcessTime = HAL_GetTick();
}

void AXEBMS_RS485_Stop(AXEBMS_RS485* bms) {
    // 可以在这里添加停止逻辑
}

void AXEBMS_RS485_ConfigWarmingVoltage(AXEBMS_RS485* bms, uint32_t vol, uint16_t s) {
    bms->warmingVoltage = vol;
    s *= 100;
    if (s < AXEBMS_RS485_DELAY_1S)
        s = AXEBMS_RS485_DELAY_1S;
    bms->warmingVoltageDelayMs = s;
}

void AXEBMS_RS485_ConfigProtectVoltage(AXEBMS_RS485* bms, uint32_t vol, uint16_t s) {
    bms->protectVoltage = vol;
    s *= 100;
    if (s < AXEBMS_RS485_DELAY_1S)
        s = AXEBMS_RS485_DELAY_1S;
    bms->protectVoltageDelayMs = s;
}


void AXEBMS_RS485_Process(AXEBMS_RS485* bms) {
    uint32_t currentTime = HAL_GetTick();
    
    
    // **重要**：总线的Process函数需要一直被调用，以处理接收和超时
    ModbusRtuBus_Process();
    // 每500ms执行一次
    if (currentTime - lastProcessTime >= AXEBMS_RS485_TICK_MS) {
        lastProcessTime = currentTime;
        
        // 1. 发送请求
        // 通过Modbus总线发送，总线会自己处理CRC和状态切换。
        // 超时设置为200毫秒。
        ModbusRtuBus_ReadHoldingRegisters(bms->devAddr, 0x1003, 16, 200);
        
        
        
        // 2. 处理业务逻辑 (基于 validCnt 的状态)
        if (bms->validCnt > 0) {
            bms->validCnt = 0; // 清除标志，等待下一次回调来重新置位
            
            // 电压保护逻辑
            if (bms->protectVoltage > 0) {
                if (bms->data.Voltage < bms->protectVoltage)
                    bms->protectVoltageCount++;
                
                bms->protectVoltageTimer += AXEBMS_RS485_TICK_MS;
                if (bms->protectVoltageTimer >= bms->protectVoltageDelayMs) {
                    bms->kVoltageProtected = (((float)(bms->protectVoltageCount * AXEBMS_RS485_TICK_MS) / bms->protectVoltageDelayMs) > 0.5);
                    bms->protectVoltageTimer = 0;
                    bms->protectVoltageCount = 0;
                }
            }
            
            if (bms->warmingVoltage > 0) {
                if (bms->data.Voltage < bms->warmingVoltage)
                    bms->warmingVoltageCount++;
                
                bms->warmingVoltageTimer += AXEBMS_RS485_TICK_MS;
                if (bms->warmingVoltageTimer >= bms->warmingVoltageDelayMs) {
                    bms->kVoltageWarming = (((float)(bms->warmingVoltageCount * AXEBMS_RS485_TICK_MS) / bms->warmingVoltageDelayMs) > 0.7);
                    bms->warmingVoltageTimer = 0;
                    bms->warmingVoltageCount = 0;
                }
            }
            
            // 更新状态
            int8_t currState = 0;
            if (bms->kVoltageProtected)
                currState = -2;
            else if (bms->kVoltageWarming)
                currState = -1;
            
            if (currState != bms->state) {
                bms->state = currState;
                // 这里可以触发状态变化事件
            }
        } else {
            // 通信失败处理
            if (bms->state != -3) {
                bms->validCnt--;
                if (bms->validCnt <= -10) {
                    bms->state = -3;
                    // 这里可以触发通信故障事件
                }
            }
        }
    }
}

AXEBMS_Data* AXEBMS_RS485_GetData(AXEBMS_RS485* bms) {
    return &bms->data;
}


bool AXEBMS_RS485_ReadTelemetry(const AXEBMS_RS485* bms,
                                AXEBMS_Data* data,
                                uint32_t* last_good_tick,
                                bool* sample_valid)
{
    uint32_t sequence_before = 0U;
    uint32_t sequence_after = 0U;

    if ((bms == NULL) || (data == NULL) || (last_good_tick == NULL) ||
        (sample_valid == NULL))
    {
        return false;
    }

    /* Motion can preempt the lower-priority BMS writer: never spin on odd. */
    for (unsigned retry=0U; retry<3U; ++retry)
    {
        sequence_before = bms->sample_sequence;
        if ((sequence_before & 1U) != 0U)
        {
            return false;
        }
        __DMB();
        *data = bms->data;
        *last_good_tick = bms->last_good_tick;
        *sample_valid = bms->sample_valid;
        __DMB();
        sequence_after = bms->sample_sequence;
        if (sequence_before == sequence_after) return true;
    }
    return false;
}
int8_t AXEBMS_RS485_GetState(AXEBMS_RS485* bms) {
    return bms->state;
}

bool AXEBMS_RS485_IsLowVoltage(AXEBMS_RS485* bms) {
    return bms->state == -1;
}

bool AXEBMS_RS485_IsLossVoltage(AXEBMS_RS485* bms) {
    return bms->state == -2;
}
