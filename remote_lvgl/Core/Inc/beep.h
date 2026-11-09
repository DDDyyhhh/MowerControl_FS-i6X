#ifndef __BEEP_H
#define __BEEP_H

#include "main.h"

// 定义声音类型
typedef enum {
    BEEP_NONE = 0,
    BEEP_SHORT,         // 短鸣 (普通操作反馈)
    BEEP_CONNECT,       // 连接成功 (升调)
    BEEP_DISCONNECT,    // 断开连接 (降调)
    BEEP_ALARM_LOW_BAT, // 低电量报警
    BEEP_ALARM_FAULT    // 故障报警 (急促)
} BeepType_t;

void BEEP_Init(void);
void BEEP_Set_Freq(uint32_t freq);
void BEEP_Off(void);
void Play_Beep(BeepType_t type); // 发送请求的接口

#endif