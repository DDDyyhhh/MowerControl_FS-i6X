#ifndef BLADE_HIL_UART_DEBUG_H
#define BLADE_HIL_UART_DEBUG_H

#include <stdint.h>

#include "main.h"

#ifndef BLADE_HIL_UART_DEBUG_ENABLED
#define BLADE_HIL_UART_DEBUG_ENABLED 0
#endif

#ifndef SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#define SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED 0
#endif

#if ((BLADE_HIL_UART_DEBUG_ENABLED != 0) && \
     (BLADE_HIL_UART_DEBUG_ENABLED != 1))
#error "BLADE_HIL_UART_DEBUG_ENABLED must be 0 or 1"
#endif

#if ((SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED != 0) && \
     (SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED != 1))
#error "SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED must be 0 or 1"
#endif

#if BLADE_HIL_UART_DEBUG_ENABLED && SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#error "Blade HIL USART2 debug and SENS i-BUS transport are mutually exclusive"
#endif

#if BLADE_HIL_UART_DEBUG_ENABLED
void BladeHilDebug_Init(void);
void BladeHilDebug_Printf(const char *format, ...);
void BladeHilDebug_OnTxComplete(UART_HandleTypeDef *huart);
#else
#define BladeHilDebug_Init() ((void)0)
#define BladeHilDebug_Printf(...) ((void)0)
#define BladeHilDebug_OnTxComplete(huart) ((void)(huart))
#endif

#endif
