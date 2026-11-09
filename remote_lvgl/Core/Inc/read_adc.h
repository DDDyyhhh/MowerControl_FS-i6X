#ifndef __STATUS_H_
#define __STATUS_H_

#include <stdint.h>
#include "main.h"
#include "adc.h"
uint16_t Read_ADC_Channel(ADC_HandleTypeDef *hadc, uint32_t channel);
void Read_Joystick();
void Send_Joystick_Data(uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry);
#endif
