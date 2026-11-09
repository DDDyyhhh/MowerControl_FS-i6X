#ifndef __PAIRING_REMOTE_H
#define __PAIRING_REMOTE_H

#include "main.h"
#include "pairing_common.h"

// ============================================================
// 全局变量 (供 GUI 显示)
// ============================================================
extern volatile uint16_t new_address; // 生成的新地址
extern volatile bool g_is_pairing_mode; // true: 正在配对, false: 正常
// ============================================================
// 函数声明
// ============================================================

/**
 * @brief 启动阻塞式配对流程
 * @note  调用此函数后，任务会进入无限循环，直到配对成功。
 *        期间会不断发送请求，并等待接收端响应。
 */
void Remote_Pairing_Process_Blocking(void);

/**
 * @brief 配对消息处理回调
 * @note  由 app_comm.c 中的 RxTask 调用
 */
void Pairing_Remote_Handle_Message(uint8_t *data, uint16_t length);


void pairing_remote_process(void);

#endif