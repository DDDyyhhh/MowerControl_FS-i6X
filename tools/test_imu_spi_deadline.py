#!/usr/bin/env python3
"""Execute the actual HAL SPI wait/end helpers with deterministic flag and tick IO."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'control_chassis/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c'
s=path.read_text()
def function(name):
 start=s.index('static HAL_StatusTypeDef '+name+'(',s.index('static HAL_StatusTypeDef '+name+'(')+1)
 brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
FIXTURE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef enum {HAL_OK=0,HAL_ERROR=1,HAL_BUSY=2,HAL_TIMEOUT=3} HAL_StatusTypeDef;
typedef enum {RESET=0,SET=1} FlagStatus;
typedef struct {struct {unsigned Mode,Direction,CRCCalculation;} Init;unsigned State,ErrorCode,unlocked;} SPI_HandleTypeDef;
#define __IO volatile
#define HAL_MAX_DELAY UINT32_MAX
#define SPI_MODE_MASTER 1
#define SPI_DIRECTION_1LINE 1
#define SPI_DIRECTION_2LINES_RXONLY 2
#define SPI_CRCCALCULATION_ENABLE 1
#define SPI_IT_TXE 1
#define SPI_IT_RXNE 2
#define SPI_IT_ERR 4
#define SPI_FLAG_TXE 1
#define SPI_FLAG_BSY 2
#define SPI_BSY_FLAG_WORKAROUND_TIMEOUT 1000
#define HAL_SPI_ERROR_FLAG 1
#define HAL_SPI_STATE_READY 0
#define __HAL_SPI_DISABLE_IT(h,x) ((void)(h),(void)(x))
#define __HAL_SPI_DISABLE(h) ((void)(h))
#define SPI_RESET_CRC(h) ((void)(h))
#define __HAL_UNLOCK(h) ((h)->unlocked=1)
#define SET_BIT(a,b) ((a)|=(b))
#define SystemCoreClock 168000000U
static uint32_t now, step, tick_reads, flags, flag_reads, release_after;
static uint32_t HAL_GetTick(void){uint32_t t=now;now+=step;++tick_reads;assert(tick_reads<100);return t;}
static unsigned ReadFlag(uint32_t bit){++flag_reads;assert(flag_reads<20000);if(release_after&&flag_reads>=release_after)flags|=SPI_FLAG_TXE;return (flags&bit)!=0;}
#define __HAL_SPI_GET_FLAG(h,f) ((void)(h),ReadFlag(f))
'''
TEST=r'''
static void Reset(uint32_t time,uint32_t increment){now=time;step=increment;tick_reads=flag_reads=flags=release_after=0;}
int main(void){
 SPI_HandleTypeDef h={0};h.Init.Mode=SPI_MODE_MASTER;
 /* Deadline already expired at entry: no unsigned remaining-time wrap. */
 Reset(103,0);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,100)==HAL_TIMEOUT);assert(flag_reads==1&&tick_reads<=2&&h.unlocked);
 Reset(102,0);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,100)==HAL_TIMEOUT);assert(flag_reads==1);
 Reset(101,1);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,100)==HAL_TIMEOUT);assert(now<=103);
 Reset(0,0);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,UINT32_MAX-2)==HAL_TIMEOUT);assert(flag_reads==1);
 Reset(0,1);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,UINT32_MAX)==HAL_TIMEOUT);
 Reset(100,0);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,0,100)==HAL_TIMEOUT);
 Reset(100,0);flags=SPI_FLAG_TXE;assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,100)==HAL_OK);
 /* HAL_MAX_DELAY still waits until flag arrives, regardless of age. */
 Reset(100,0);release_after=4;assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,HAL_MAX_DELAY,0)==HAL_OK);
 /* Frozen tick fallback remains bounded. */
 Reset(100,0);assert(SPI_WaitFlagStateUntilTimeout(&h,SPI_FLAG_TXE,SET,2,100)==HAL_TIMEOUT);assert(flag_reads<20000);
 /* TXE succeeds but BSY remains high after the shared deadline. */
 Reset(103,0);flags=SPI_FLAG_TXE|SPI_FLAG_BSY;assert(SPI_EndRxTxTransaction(&h,2,100)==HAL_TIMEOUT);assert(flag_reads==2);
 Reset(100,0);flags=SPI_FLAG_TXE;assert(SPI_EndRxTxTransaction(&h,2,100)==HAL_OK);
 puts("PASS HAL SPI shared deadline: 0/1/2/expired, tick wrap, frozen tick, infinite wait and stuck BSY");
}
'''
# A frozen tick invokes GetTick repeatedly; tick-read bound applies to advancing clock only.
FIXTURE=FIXTURE.replace('assert(tick_reads<100);','assert(!step || tick_reads<100);')
with tempfile.TemporaryDirectory(prefix='imu-hal-deadline-') as td:
 p=Path(td);(p/'test.c').write_text(FIXTURE+function('SPI_WaitFlagStateUntilTimeout')+function('SPI_EndRxTxTransaction')+TEST)
 for opt in ['-O0','-O2']:
  subprocess.run([os.environ.get('CC','cc'),'-std=c99',opt,'-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,timeout=10)
