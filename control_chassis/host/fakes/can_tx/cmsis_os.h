#ifndef CAN_TX_FAKE_OS_H
#define CAN_TX_FAKE_OS_H
#include <stdint.h>
typedef uintptr_t osMessageQueueId_t;
typedef int osStatus_t;
#define osOK 0
#define osError 1
osStatus_t osMessageQueuePut(osMessageQueueId_t, const void *, uint8_t, uint32_t);
osStatus_t osMessageQueueGet(osMessageQueueId_t, void *, uint8_t *, uint32_t);
#endif
