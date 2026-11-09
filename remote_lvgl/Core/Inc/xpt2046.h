#ifndef __XPT2046_H
#define __XPT2046_H

#include "main.h"

// 屏幕分辨率 (根据你的实际屏幕修改)
#define TOUCH_SCREEN_WIDTH  320
#define TOUCH_SCREEN_HEIGHT 240

// 校准参数 (先用默认值，如果方向反了或不准需要调整)
// 这里的数值是原始ADC值 (0-4095)
#define X_MIN 200
#define X_MAX 3900
#define Y_MIN 200
#define Y_MAX 3800

typedef struct {
    uint16_t x;
    uint16_t y;
} Touch_Coordinate;

// 初始化
void XPT2046_Init(void);
// 检测是否按下 (0:按下, 1:未按下) - 对应 IRQ 引脚
uint8_t XPT2046_IsPressed(void);
// 获取经过校准和滤波的坐标
uint8_t XPT2046_Get_Coordinates(uint16_t *px, uint16_t *py);

#endif