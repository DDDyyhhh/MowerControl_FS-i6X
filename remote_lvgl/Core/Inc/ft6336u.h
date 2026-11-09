#ifndef __FT6336U_H__
#define __FT6336U_H__

#include "main.h"
#include "stdbool.h"
#include "main.h"
#define FT6336U_ADDR_WRITE 0x70
#define FT6336U_ADDR_READ 0x71


// Pin
#define FT6336U_CS_PORT GPIOE
#define FT6336U_CS_PIN GPIO_PIN_6


// 触摸点结构体
typedef struct
{
    bool is_pressed;
    uint16_t x;
    uint16_t y;
} FT6336U_TouchPoint_t;

void FT6336U_Init(void);
bool FT6336U_ReadTouch(FT6336U_TouchPoint_t *touch);

#endif