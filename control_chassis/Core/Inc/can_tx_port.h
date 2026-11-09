#ifndef CAN_TX_PORT_H
#define CAN_TX_PORT_H

#include "can_tx_scheduler.h"

typedef enum
{
    CAN_TX_PENDING = 0,
    CAN_TX_SUCCESS,
    CAN_TX_ARBITRATION_LOST,
    CAN_TX_TRANSMIT_ERROR,
    CAN_TX_ABORTED
} CAN_TxPortResult_t;

/* Internal hardware seam, exclusively called by CANTx. No wait loops. Poll
 * consumes exactly this mailbox's completion; submit requires a settled, empty
 * mailbox. TX interrupts stay disabled so RX IRQ cannot consume these flags. */
bool CAN_TxPort_IsEmpty(uint8_t mailbox);
bool CAN_TxPort_Submit(uint8_t mailbox, const CAN_TxMsg_t *msg);
CAN_TxPortResult_t CAN_TxPort_Poll(uint8_t mailbox);
void CAN_TxPort_Abort(uint8_t mailbox);
uint32_t CAN_TxPort_GetError(void);
bool CAN_TxPort_IsBusOff(void);
#endif
