#ifndef FLIPSKY_CAN_H
#define FLIPSKY_CAN_H

#include <stdint.h>

void Flipsky_CAN_Init(void);
uint8_t Flipsky_CAN_SetSpeedErpm(float erpm);

#endif
