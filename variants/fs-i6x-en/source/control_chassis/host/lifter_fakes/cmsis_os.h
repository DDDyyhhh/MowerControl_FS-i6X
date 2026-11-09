#ifndef BAG_TEST_CMSIS_OS_H
#define BAG_TEST_CMSIS_OS_H
#include <stdint.h>
typedef void *osMutexId_t;
typedef int osStatus_t;
typedef struct { const char *name; uint32_t attr_bits; } osMutexAttr_t;
#define osMutexPrioInherit 1U
#define osWaitForever UINT32_MAX
#define osOK 0
osMutexId_t osMutexNew(const osMutexAttr_t *attr);
osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex);
#endif
