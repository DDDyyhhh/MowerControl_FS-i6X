#include "can_tx_port.h"
#include "can.h"

static bool CAN_TxPort_Ready(void)
{
    return hcan1.State == HAL_CAN_STATE_READY ||
           hcan1.State == HAL_CAN_STATE_LISTENING;
}

bool CAN_TxPort_IsEmpty(uint8_t mailbox)
{
    return mailbox < 3U && CAN_TxPort_Ready() &&
           (hcan1.Instance->TSR & (CAN_TSR_TME0 << mailbox)) != 0U;
}

bool CAN_TxPort_Submit(uint8_t mailbox, const CAN_TxMsg_t *msg)
{
    CAN_TxMailBox_TypeDef *target;
    if (!msg || msg->dlc > 8U || !CAN_TxPort_IsEmpty(mailbox) ||
        (hcan1.Instance->TSR & (CAN_TSR_RQCP0 << (8U * mailbox))) != 0U) {
        return false;
    }
    /* Same register layout as HAL_CAN_AddTxMessage, but select the mailbox
     * explicitly. Never reuse a completed mailbox before Poll settles it. */
    target = &hcan1.Instance->sTxMailBox[mailbox];
    if (msg->ide == CAN_ID_EXT) {
        target->TIR = ((msg->extId & 0x1FFFFFFFU) << CAN_TI0R_EXID_Pos) | CAN_ID_EXT;
    } else {
        target->TIR = (msg->stdId & 0x7FFU) << CAN_TI0R_STID_Pos;
    }
    if (msg->rtr != 0U) target->TIR |= CAN_RTR_REMOTE;
    target->TDTR = msg->dlc;
    target->TDLR = (uint32_t)msg->data[0] | ((uint32_t)msg->data[1] << 8) |
                   ((uint32_t)msg->data[2] << 16) | ((uint32_t)msg->data[3] << 24);
    target->TDHR = (uint32_t)msg->data[4] | ((uint32_t)msg->data[5] << 8) |
                   ((uint32_t)msg->data[6] << 16) | ((uint32_t)msg->data[7] << 24);
    __COMPILER_BARRIER();
    target->TIR |= CAN_TI0R_TXRQ;
    return true;
}

CAN_TxPortResult_t CAN_TxPort_Poll(uint8_t mailbox)
{
    uint32_t status;
    if (mailbox >= 3U) return CAN_TX_PENDING;
    status = hcan1.Instance->TSR >> (8U * mailbox);
    if ((status & CAN_TSR_RQCP0) == 0U) return CAN_TX_PENDING;
    /* RQCP is W1C and clears this mailbox's TXOK/ALST/TERR together. */
    hcan1.Instance->TSR = CAN_TSR_RQCP0 << (8U * mailbox);
    if ((status & CAN_TSR_TXOK0) != 0U) return CAN_TX_SUCCESS;
    if ((status & CAN_TSR_ALST0) != 0U) return CAN_TX_ARBITRATION_LOST;
    if ((status & CAN_TSR_TERR0) != 0U) return CAN_TX_TRANSMIT_ERROR;
    return CAN_TX_ABORTED;
}

void CAN_TxPort_Abort(uint8_t mailbox)
{
    if (mailbox < 3U && CAN_TxPort_Ready()) {
        /* Do not use a TSR read-modify-write: it would clear unrelated RQCP. */
        hcan1.Instance->TSR = CAN_TSR_ABRQ0 << (8U * mailbox);
    }
}

uint32_t CAN_TxPort_GetError(void)
{
    return HAL_CAN_GetError(&hcan1);
}
