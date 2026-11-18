#ifndef IBUS_SNAPSHOT_IRQ_H
#define IBUS_SNAPSHOT_IRQ_H

#include <stdint.h>

/* CMSIS port seam: the harness models preemption before masking and on restore. */
uint32_t IbusTest_GetPrimask(void);
void IbusTest_DisableIrq(void);
void IbusTest_SetPrimask(uint32_t mask);
void IbusTest_CompilerBarrier(void);
#define __get_PRIMASK IbusTest_GetPrimask
#define __disable_irq IbusTest_DisableIrq
#define __set_PRIMASK IbusTest_SetPrimask
#define __COMPILER_BARRIER IbusTest_CompilerBarrier

#endif
