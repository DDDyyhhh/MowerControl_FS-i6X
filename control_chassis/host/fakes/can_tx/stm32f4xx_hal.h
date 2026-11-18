#ifndef CAN_TX_FAKE_HAL_H
#define CAN_TX_FAKE_HAL_H
#include <stdint.h>
#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
uint32_t HAL_GetTick(void);
#endif
