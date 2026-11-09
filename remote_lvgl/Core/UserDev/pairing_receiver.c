// // pairing_receiver.c
// /**
//   ******************************************************************************
//   * @file           : pairing_remote.c / pairing_receiver.c
//   * @brief          : 遥控器/接收器双向动态配对状态机 (Pairing FSM)
//   * 
//   * 主要功能：
//   * 1. 管理多机防串频对码流程：
//   *    [REQ 请求] -> [ACK 响应] -> [ADDR 派发新地址] ->[CONFIRM 确认] -> [TEST 测试连通性]
//   * 2. `Remote_Pairing_Process_Blocking` 中实现了完整的重试、超时与失败恢复回退机制
//   *
//   * 注意事项：
//   * - 流程中大量利用了 RTOS 的 `osDelay`，确保接收任务 (RxTask) 能够在后台持续接收对面的确认包
//   ******************************************************************************
//   */
 
// #include "pairing_receiver.h"
// #include "usart.h"
// #include <stdlib.h>
// #include <string.h>
// #include <pairing_common.h>
// #include "E34.h"
// // 内部状态
// static pairing_state_t pairing_state = PAIRING_IDLE;
// extern volatile uint16_t new_address1;
// static uint32_t pairing_timer = 0;
// volatile uint8_t REQ = 0;
// volatile uint8_t ADDR = 0;
// extern volatile uint8_t TESTok;
// extern uint16_t voltage_int;
// extern uint16_t currentA_int;

// pairing_state_t pairing_receiver_get_state(void)
// {
//     return pairing_state;
// }

// void pairing_receiver_init(void)
// {
//     pairing_state = PAIRING_IDLE;
//     new_address1 = 0;
//     pairing_timer = 0;
//     TESTok = 0;
//     // 确保使用默认地址开始
//     E34_Set_Address(DEFAULT_ADDRESS);
    
// }

// void pairing_receiver_handle_message(uint8_t *data, uint16_t length)
// {

//     uint8_t cmd = data[1]; // 命令字节在第二位

//     switch (cmd)
//     {
//     case PAIRING_REQ:
//         if (pairing_state == PAIRING_IDLE)
//         {
//             REQ = 1;
//         }

//         break;

//     case PAIRING_ADDR:
//         if (pairing_state == PAIRING_STARTED)
//         {
//             // 地址在 data[2] 和 data[3]

//             new_address1 = (data[2] << 8) | data[3];
//             ADDR = 1;
//         }

//         break;

//     case PAIRING_TEST:
//         if (pairing_state == PAIRING_ADDR_RECEIVED)
//         {

//             TESTok = 1;
//         }
//         break;
//     }
// }

// void pairing_receiver_task(void)
// {

//     if (pairing_state == PAIRING_IDLE && REQ == 1)
//     {
//         uint8_t buf[3] = {0xAA, PAIRING_ACK, 0xBB};
//         HAL_UART_Transmit(&huart3, buf, 3, HAL_MAX_DELAY);
//         HAL_Delay(50);
//         REQ = 0;
//         pairing_state = PAIRING_STARTED;
//     }

//     if (pairing_state == PAIRING_STARTED && ADDR == 1)
//     {
//         E34_Set_Address(new_address1);
//         HAL_Delay(50);
//         ADDR = 0;
//         HAL_Delay(100);
//         uint8_t buf[3] = {0xAA, PAIRING_CONFIRM, 0xBB};

//         HAL_UART_Transmit(&huart3, buf, 3, HAL_MAX_DELAY);
//         HAL_Delay(50);
//         pairing_state = PAIRING_ADDR_RECEIVED;
//     }

//     if (pairing_state == PAIRING_ADDR_RECEIVED && TESTok == 1)
//     {

//         uint8_t buf[3] = {0xAA, PAIRING_TEST_OK, 0xBB};
//         HAL_UART_Transmit(&huart3, buf, 3, HAL_MAX_DELAY);
//         HAL_Delay(50);
//         pairing_state = PAIRING_SUCCESS;
//     }
// }

// extern uint8_t receiver_status[8];

// void receiver_resport(void)
// {
//     uint8_t buf[10];
//     buf[0] = 0xCC; // 帧头
//     buf[1] = receiver_status[0];
//     buf[2] = receiver_status[1];
//     buf[3] = receiver_status[2];
//     // 拆分为高低字节
//     buf[4] = (voltage_int >> 8) & 0xFF;
//     buf[5] = voltage_int & 0xFF;
//     buf[6] = (currentA_int >> 8) & 0xFF;
//     buf[7] = currentA_int & 0xFF;
//     buf[8] = 0;

//     buf[9] = 0xDD; // 帧尾

//     HAL_UART_Transmit(&huart3, buf, 10, HAL_MAX_DELAY);
// }
// // status[0]刀片状态 0锁定 1解锁
// // status[1]割草机速度状态  1低速 2中速 3高速
// // status[2]割草机刀片速度状态 0停止 1低速 2中速 3高速
// // status[3]电池电压 V如电压3.6V存36
// // status[4]电池电流 A电流1.2A存12
// // status[5] 刀盘高度
// // status[6] 草袋
// // status[7] 超声波0距离
// // status[8] 超声波1距离
// // status[9] 超声波2距离
// // status[10] 超声波3距离
// // status[11] 异常状态
// //status[12] 解锁请求
// //
// // --- 0: 正常/手动模式 ---
// // REPORT_NONE = 0,         

// // // --- 1~9: 严重故障 (会导致停车/红灯) ---
// // REPORT_FAULT_COLLISION = 1,        // 碰撞杆触发
// // REPORT_FAULT_EMERGENCY_BTN = 2,    // 急停按钮触发
// // REPORT_FAULT_TILT = 3,             // 倾翻触发
// // REPORT_FAULT_COMM_LOST = 4,        // 通信丢失

// // // --- 10~19: 巡航工作状态 (正常运行/绿灯或蓝灯) ---
// // REPORT_CRUISE_STRAIGHT = 10,       // 直线巡航
// // REPORT_CRUISE_LEFT = 11,           // 左转/左掉头中
// // REPORT_CRUISE_RIGHT = 12           // 右转/右掉头中