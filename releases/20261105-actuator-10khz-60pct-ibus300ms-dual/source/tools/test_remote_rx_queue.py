#!/usr/bin/env python3
"""Run target UART/queue adapters over a deterministic 16-slot CMSIS queue.

The parser, authorization and safety gate are production C. HOST only removes
board drivers; the actual target callback/Process/OnUartRx functions are compiled
again with their target branches, not replaced by a synchronous host adapter.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT/'control_chassis/Core'

def function(source, name):
    match = re.search(r'void '+name+r'\([^;]*?\)\s*\{', source, re.S)
    if not match: raise ValueError(name)
    start=match.start();end=source.index('{', start)+1;depth=1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

PREFIX=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "remote_backend.h"
#include "safety_ownership_gate.h"
#define RemoteBackend_OnUartRx HostDirect_OnUartRx
#define RemoteBackend_Process HostDirect_Process
#define RemoteBackend_OnUartError HostDirect_OnUartError
#include "@BACKEND@"
#undef RemoteBackend_OnUartRx
#undef RemoteBackend_Process
#undef RemoteBackend_OnUartError
#undef REMOTE_BACKEND_HOST
#define osOK 0
#define RX_BUF_SIZE 64
#define USART1 ((void *)1)
#define USART3 ((void *)3)
#define UART4 ((void *)4)
#define USART6 ((void *)6)
typedef struct {volatile unsigned DR;} UartRegisters;
typedef struct {UartRegisters *Instance;uint32_t ErrorCode;} UART_HandleTypeDef;
static UART_HandleTypeDef huart1={(void *)1,0U},huart3={(void *)3,0U};
static uint8_t rx_dma_buffer[64];
static uint32_t clock_ms;
static unsigned protocol_calls, pairing_calls;
static uint32_t protocol_last_ms;
static void *Queue_RxDataHandle=(void *)1;
typedef struct {uint8_t data[64];uint16_t len;} UartRxMsg_t;
#ifdef REMOTE_RX_BUFFER_SIZE
 typedef RemoteRxMsg_t TestMessage;
#else
 typedef UartRxMsg_t TestMessage;
#endif
static TestMessage messages[16];static unsigned qread,qwrite,qcount;
static const size_t configured_size=@QUEUE_SIZE@;
static const size_t cubemx_size=@CUBEMX_SIZE@;
static int osMessageQueuePut(void *queue,const void *message,unsigned priority,unsigned timeout){
 (void)queue;assert(priority==0&&timeout==0);if(qcount==16)return -1;

#ifdef REMOTE_RX_BUFFER_SIZE
 assert(configured_size==sizeof(TestMessage)&&cubemx_size==configured_size);
#endif
 memcpy(&messages[qwrite++%16],message,sizeof(TestMessage));++qcount;return osOK;
}
static int osMessageQueueGet(void *queue,void *message,void *priority,unsigned timeout){
 (void)queue;(void)priority;(void)timeout;if(!qcount)return -1;
 memcpy(message,&messages[qread++%16],sizeof(TestMessage));--qcount;return osOK;
}
static inline uint32_t HAL_GetTick(void){return clock_ms;}
static inline int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef*h,uint8_t*b,uint16_t n){(void)h;(void)b;assert(n==64);return 0;}
static inline void HAL_UART_AbortReceive(UART_HandleTypeDef*h){(void)h;}
static inline void HAL_UART_DMAStop(UART_HandleTypeDef*h){(void)h;}
#define __HAL_UART_CLEAR_OREFLAG(h) ((void)(h))
#define __HAL_UART_CLEAR_NEFLAG(h) ((void)(h))
#define __HAL_UART_CLEAR_FEFLAG(h) ((void)(h))
static inline void RemoteBackend_ApplyCommand(void){}
static inline void pairing_receiver_handle_message(uint8_t *data,uint16_t n){(void)data;(void)n;++pairing_calls;}
static inline void RS485_RxEventCallback(UART_HandleTypeDef*h,uint16_t n){(void)h;(void)n;}
static inline void SR04_RxEventCallback(UART_HandleTypeDef*h,uint16_t n){(void)h;(void)n;}
void exp3_board_channels(uint32_t now,uint16_t ch4,uint16_t a,uint16_t b){protocol_last_ms=now;(void)ch4;(void)a;(void)b;++protocol_calls;}
'''

def run():
    backend=(CORE/'UserDev/remote_backend.c').read_text()
    callback=function((CORE/'Src/main.c').read_text(),'HAL_UARTEx_RxEventCallback')
    queue_size=re.search(r'Queue_RxDataHandle\s*=\s*osMessageQueueNew\s*\(16,\s*(.*?),\s*&Queue_RxData_attributes', (CORE/'Src/freertos.c').read_text()).group(1)
    # Retain the real legacy parser too, including pairing rejection assertions.
    cubemx_size=re.search(r'Queue_RxData,16,(\d+),1,Dynamic', (ROOT/'control_chassis/concrol.ioc').read_text()).group(1)
    adapter=PREFIX.replace('@BACKEND@',str(CORE/'UserDev/remote_backend.c')).replace('@QUEUE_SIZE@',queue_size).replace('@CUBEMX_SIZE@',cubemx_size)
    adapters='\n'.join(function(backend,n) for n in ['RemoteBackend_Process','RemoteBackend_OnUartRx','RemoteBackend_OnUartError'])
    tests=(ROOT/'control_chassis/host/remote_rx_queue_harness.c').read_text()
    with tempfile.TemporaryDirectory(prefix='remote-rx-queue-') as td:
        p=Path(td);(p/'queue.c').write_text(adapter+adapters+callback+tests)
        for timeout_ms in (100,300):
            for optimization in ('-O0','-O2'):
                for flysky in (0,1):
                    cmd=[os.environ.get('CC','cc'),'-std=c99',optimization,'-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                         '-DREMOTE_BACKEND_HOST',f'-DREMOTE_IBUS_TIMEOUT_MS={timeout_ms}U',f'-DREMOTE_BACKEND_A28_E34={1-flysky}',f'-DREMOTE_BACKEND_FLYSKY_IBUS={flysky}',
                         f'-DMOWER_BLADE_OK_ENABLED={flysky}',f'-DMOWER_GRASS_BAG_CH6_ENABLED={flysky}',
                         '-DEXP3_ENABLED=1','-I'+str(CORE/'Inc'),str(p/'queue.c')]
                    cmd += [str(CORE/'UserDev'/n) for n in ('flysky_ibus_servo.c','remote_authorization.c','blade_confirm_ch6_adapter.c','grass_bag_control.c')]
                    cmd += [str(CORE/'UserApp/safety_ownership_gate.c'),'-lm','-o',str(p/'test')]
                    subprocess.run(cmd,check=True)
                    subprocess.run([str(p/'test')],check=True,timeout=5,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
                    print(f'PASS {optimization} flysky={flysky} timeout_ms={timeout_ms}',flush=True)
if __name__=='__main__':run()
