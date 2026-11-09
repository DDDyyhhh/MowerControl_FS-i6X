#ifndef __KEY_H
#define __KEY_H

#include <stdint.h> 
/* 按键事件类型定义 */
typedef enum {
    KEY_EVENT_NONE = 0,     // 无事件
    KEY_EVENT_SHORT=1,        // 短按
    KEY_EVENT_LONG=2         // 长按
} KeyEvent;

/* 按键状态机结构体 */
typedef struct {
    uint8_t state;
    uint32_t press_time;
    uint8_t last_level;
    KeyEvent event;
} KeyFSM;

/* 时间阈值定义 */
#define KEY_DEBOUNCE_TIME   20
#define KEY_LONG_PRESS_TIME 2000

/* 函数声明 */
void KEY_Init(void);
uint8_t KEY_Read(uint8_t key_num);
void KEY_Scan(uint32_t interval_ms);
KeyEvent KEY_GetEvent(uint8_t key_num);
KeyEvent key_event(uint8_t key_num);

void Send_Key_Status(void);
void Send_Key_Events(void);
#endif
