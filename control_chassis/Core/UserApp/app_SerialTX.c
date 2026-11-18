/**
 ******************************************************************************
 * @file           : app_SerialTX.c
 * @brief          : Shared TX task for UART1/UART2 (debug and local links)
 ******************************************************************************
 */

#include "app_context.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "serial_port.h"
#include "chassis_control.h"
#include "odrive_can.h"
#include <string.h>

extern osMessageQueueId_t Queue_TxDataWirelessHandle;
extern osSemaphoreId_t Sem_TxCompleteWirelessHandle;
extern osMessageQueueId_t Queue_CanTxHandle;

void StartTask_WirelessTx(void *argument)
{
  UartTxMsg_t msg;

  for (;;)
  {
    if (osMessageQueueGet(Queue_TxDataWirelessHandle, &msg, NULL, osWaitForever) == osOK)
    {
      if (msg.port_id != SERIAL_PORT_3)
      {
        continue;
      }

      if (HAL_UART_Transmit_DMA(&huart3, msg.buffer, msg.len) == HAL_OK)
      {
        (void)osSemaphoreAcquire(Sem_TxCompleteWirelessHandle, 100);
      }
    }
  }
}

void StartTask_CANTx(void *argument)
{
    (void)argument;
    for (;;) {
        if (!CAN_TxScheduler_Process()) {
            osDelay(1U);
        }
    }
}
