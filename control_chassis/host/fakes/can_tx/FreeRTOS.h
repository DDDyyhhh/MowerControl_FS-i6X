#ifndef CAN_TX_FAKE_FREERTOS_H
#define CAN_TX_FAKE_FREERTOS_H
void CanTxTest_EnterCritical(void);
void CanTxTest_ExitCritical(void);
#define taskENTER_CRITICAL() CanTxTest_EnterCritical()
#define taskEXIT_CRITICAL() CanTxTest_ExitCritical()
#endif
