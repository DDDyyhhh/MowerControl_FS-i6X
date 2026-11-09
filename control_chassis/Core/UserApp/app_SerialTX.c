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
    CAN_TxMsg_t msg;
    CAN_TxHeaderTypeDef tx_header;
    uint32_t mailbox;
    osStatus_t status;

    for (;;)
    {
        // 控制帧优先，并且只发送每个节点的最新速度指令。
        if (CAN_TryGet_Control_Message(&msg))
        {
            status = osOK;
        }
        else if (CAN_TryGet_Telemetry_Message(&msg))
        {
            status = osOK;
        }
        else
        {
            status = osMessageQueueGet(Queue_CanTxHandle, &msg, NULL, 1U);
        }
        
        if (status == osOK)
        {
            // 2. 组装硬件帧头
            memset(&tx_header, 0, sizeof(tx_header));
            tx_header.IDE = (msg.ide == CAN_ID_EXT) ? CAN_ID_EXT : CAN_ID_STD;
            if (tx_header.IDE == CAN_ID_EXT)
            {
                tx_header.ExtId = msg.extId & 0x1FFFFFFFU;
            }
            else
            {
                tx_header.StdId = msg.stdId & 0x7FFU;
            }

            tx_header.RTR   = (msg.rtr == 1) ? CAN_RTR_REMOTE : CAN_RTR_DATA;
            tx_header.DLC   = msg.dlc;
            tx_header.TransmitGlobalTime = DISABLE;

            // 3. 硬件邮箱管理 (防止硬件死锁)
            uint32_t start_tick = HAL_GetTick();
            while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0) 
            {
                osDelay(1); // 释放 CPU，给硬件1ms时间去物理线上发报文
                
                // 【核心修复】：如果 5ms 内硬件邮箱依然满，强行中止卡死的硬件邮箱，释放空间！
                if (HAL_GetTick() - start_tick > 5) {
                    HAL_CAN_AbortTxRequest(&hcan1, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
                    goto NEXT_FRAME; 
                }
            }

            // 4. 正式写入 STM32 硬件发送邮箱
            #if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && MOTOR_DRIVER_USED == DRIVER_TYPE_OID
            HAL_StatusTypeDef tx_result = HAL_CAN_AddTxMessage(
                &hcan1, &tx_header, msg.data, &mailbox);
            if ((tx_header.IDE == CAN_ID_STD) &&
                (tx_header.RTR == CAN_RTR_DATA) && (msg.dlc == 2U) &&
                (tx_header.StdId > 0U) && (tx_header.StdId < OID_MAX_NODES) &&
                (msg.data[0] == OID_CMD_READ_PARAM) &&
                (msg.data[1] == OID_PARAM_ACTUAL_SPEED))
            {
                if (tx_result == HAL_OK)
                {
                    g_oid_hil_diag[tx_header.StdId].mailbox_ok++;
                }
                else
                {
                    g_oid_hil_diag[tx_header.StdId].mailbox_fail++;
                }
            }
            #else
            HAL_CAN_AddTxMessage(&hcan1, &tx_header, msg.data, &mailbox);
            #endif
            
NEXT_FRAME:
            continue;
        }

        if (status != osOK)
        {
            osDelay(1);
        }
    }
}
