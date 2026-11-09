#ifndef __APP_COMM_H
#define __APP_COMM_H

#include "main.h"

void App_Comm_Rx_Main(void);
void Safe_UART_DMA_Send(uint8_t *data, uint16_t len);


#endif