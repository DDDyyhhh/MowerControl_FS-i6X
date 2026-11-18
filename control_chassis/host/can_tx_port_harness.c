#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "can_tx_port.h"
#include "can.h"

static CAN_TypeDef device;
CAN_HandleTypeDef hcan1 = {&device, HAL_CAN_STATE_LISTENING};
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) { assert(h == &hcan1); return 0xaabbU; }
int main(void)
{
    CAN_TxMsg_t msg = {.extId = 0x9a18, .ide = CAN_ID_EXT, .dlc = 5,
                       .data = {0, 0, 0x3e, 0x80, 0, 0, 0, 0}};
    for (uint8_t m = 0; m < 3; ++m) {
        memset(&device, 0, sizeof(device)); device.TSR = CAN_TSR_TME0 << m;
        assert(CAN_TxPort_Submit(m, &msg));
        assert(device.sTxMailBox[m].TIR == ((0x9a18U << 3) | 5U));
        assert(device.sTxMailBox[m].TDTR == 5U);
        assert(device.sTxMailBox[m].TDLR == 0x803e0000U);
        assert(device.sTxMailBox[m].TDHR == 0U);
        assert(!device.sTxMailBox[(m + 1U) % 3U].TIR);
    }
    msg.ide = CAN_ID_STD; msg.stdId = 0x507; msg.rtr = 1;
    assert(CAN_TxPort_Submit(2, &msg));
    assert(device.sTxMailBox[2].TIR == ((0x507U << 21) | 3U));
    device.TSR |= CAN_TSR_RQCP0 << 16;
    assert(!CAN_TxPort_Submit(2, &msg)); /* Completion must be settled first. */
    device.TSR = (CAN_TSR_RQCP0 | CAN_TSR_TXOK0) << 16;
    device.TSR |= CAN_TSR_RQCP0 | CAN_TSR_TERR0;
    assert(CAN_TxPort_Poll(2) == CAN_TX_SUCCESS);
    device.ESR = 0U; assert(!CAN_TxPort_IsBusOff());
    device.ESR = CAN_ESR_BOFF; assert(CAN_TxPort_IsBusOff()); device.ESR = 0U;
    assert(device.TSR == (1U << 16)); /* W1C writes only the requested RQCP. */
    device.TSR = (CAN_TSR_RQCP0 | CAN_TSR_ALST0) << 8;
    assert(CAN_TxPort_Poll(1) == CAN_TX_ARBITRATION_LOST && device.TSR == 0x100U);
    device.TSR = CAN_TSR_RQCP0 | CAN_TSR_TERR0;
    assert(CAN_TxPort_Poll(0) == CAN_TX_TRANSMIT_ERROR && device.TSR == 1U);
    device.TSR = CAN_TSR_RQCP0 << 16;
    assert(CAN_TxPort_Poll(2) == CAN_TX_ABORTED);
    device.TSR = CAN_TSR_RQCP0 | (CAN_TSR_RQCP0 << 8);
    CAN_TxPort_Abort(2);
    assert(device.TSR == (1U << 23)); /* Abort cannot echo unrelated W1C flags. */
    device.TSR = 0U; assert(CAN_TxPort_Poll(0) == CAN_TX_PENDING);
    assert(!CAN_TxPort_Submit(0, &msg));
    device.TSR = CAN_TSR_TME0; msg.dlc = 9;
    assert(!CAN_TxPort_Submit(0, &msg));
    msg.dlc = 5; hcan1.State = 0;
    assert(!CAN_TxPort_Submit(0, &msg));
    assert(!CAN_TxPort_IsEmpty(3) && CAN_TxPort_Poll(3) == CAN_TX_PENDING);
    assert(CAN_TxPort_GetError() == 0xaabbU);
    puts("PASS production STM32 TX adapter: exact mailbox registers, result decoding, W1C/abort isolation and state guards");
}
