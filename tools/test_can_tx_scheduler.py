#!/usr/bin/env python3
"""Compile the production scheduler and FTESC encoder against a deterministic port."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
core = root / 'control_chassis/Core'
# Without ABOM a bus-off CAN controller stays offline until a power cycle.
assert 'hcan1.Init.AutoBusOff = ENABLE;' in (core / 'Src/can.c').read_text(encoding='utf-8')
assert 'CAN1.ABOM=ENABLE' in (root / 'control_chassis/concrol.ioc').read_text(encoding='utf-8', errors='replace')
with tempfile.TemporaryDirectory(prefix='mower-can-tx-') as tmp:
    tmp = Path(tmp)
    (tmp / 'can.h').write_text('#include "stm32f4xx_hal.h"\ntypedef struct {uint32_t ErrorCode;} CAN_HandleTypeDef;\n')
    (tmp / 'odrive_can.h').write_bytes((core / 'Inc/odrive_can.h').read_bytes())
    (tmp / 'BladeMotor.h').write_bytes((root / 'control_chassis/host/fakes/BladeMotor.h').read_bytes())
    for optimization in ('-O0', '-O2'):
        executable = tmp / 'scheduler'
        cmd = [os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', optimization,
               '-DFLIPSKY_HOST_TEST', '-DCAN_TX_INITIAL_GENERATION=4294967294U',
               '-I' + str(tmp), '-I' + str(root / 'control_chassis/host/fakes/can_tx'),
               '-I' + str(core / 'Inc'), str(root / 'control_chassis/host/can_tx_scheduler_harness.c'),
               str(core / 'UserDev/can_tx_scheduler.c'), str(core / 'UserDev/flipsky_can.c'),
               str(core / 'UserDev/oid_can.c'), str(core / 'UserDev/BladeMotor.c'),
               str(core / 'UserApp/safety_ownership_gate.c'), '-lm', '-o', str(executable)]
        subprocess.run(cmd, check=True)
        subprocess.run([str(executable)], check=True, timeout=5)
        print('PASS actual CAN scheduler', optimization, flush=True)

    port = tmp / 'port'
    port.mkdir()
    (port / 'can.h').write_text(r"""
#ifndef CAN_TX_REGISTER_FAKE_H
#define CAN_TX_REGISTER_FAKE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { volatile uint32_t TIR, TDTR, TDLR, TDHR; } CAN_TxMailBox_TypeDef;
typedef struct { volatile uint32_t TSR, ESR; CAN_TxMailBox_TypeDef sTxMailBox[3]; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; uint32_t State; } CAN_HandleTypeDef;
extern CAN_HandleTypeDef hcan1;
#define HAL_CAN_STATE_READY 1U
#define HAL_CAN_STATE_LISTENING 2U
#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
#define CAN_RTR_REMOTE 2U
#define CAN_TI0R_TXRQ 1U
#define CAN_TI0R_EXID_Pos 3U
#define CAN_TI0R_STID_Pos 21U
#define CAN_TSR_TME0 (1U << 26)
#define CAN_ESR_BOFF 4U
#define CAN_TSR_RQCP0 1U
#define CAN_TSR_TXOK0 2U
#define CAN_TSR_ALST0 4U
#define CAN_TSR_TERR0 8U
#define CAN_TSR_ABRQ0 128U
#define __COMPILER_BARRIER() __asm volatile ("" ::: "memory")
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *);
#endif
""")
    for optimization in ('-O0', '-O2'):
        executable = port / 'port-test'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                        optimization, '-I' + str(port), '-I' + str(core / 'Inc'),
                        str(root / 'control_chassis/host/can_tx_port_harness.c'),
                        str(core / 'UserDev/can_tx_port.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=5)
