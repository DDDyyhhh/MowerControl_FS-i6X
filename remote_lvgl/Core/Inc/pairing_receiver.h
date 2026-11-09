// pairing_receiver.h
#ifndef PAIRING_RECEIVER_H
#define PAIRING_RECEIVER_H

#include "pairing_common.h"


// 初始化接收机配对模块
void pairing_receiver_init(void);

// 处理接收到的消息
void pairing_receiver_handle_message(uint8_t* data, uint16_t length);

// 获取当前配对状态
pairing_state_t pairing_receiver_get_state(void);


#endif