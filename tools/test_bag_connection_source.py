#!/usr/bin/env python3
"""Execute the production telemetry call site with PD10 and unavailable sources."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'control_chassis/Core/UserApp/app_Motion.c').read_text()
start = source.index('static void RefreshTelemetrySnapshot(', source.index('static void RefreshTelemetrySnapshot(') + 1)
opened = source.index('{', start)
end, depth = opened + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "telemetry_snapshot.h"
typedef struct {
    bool connected, link_fresh, stable_online, timeout_latched, remote_recoverable_latched;
    unsigned recoverable_latch_reason;
} RemoteCommand_t;
typedef struct { bool emergency_active, collision_active, tilt_active; } RemoteSafetyInput_t;
typedef struct { uint32_t Voltage; int32_t CurCadc; uint16_t RSOC; } AXEBMS_Data;
typedef struct { bool valid; uint32_t last_good_tick; int32_t speed_mm_per_s; } ChassisWheelFeedback_t;
typedef struct {
    bool erpm_valid, crc_error_active;
    uint32_t erpm_last_rx_tick;
    int32_t erpm;
} FlipskyCanFeedback_t;
enum { REPORT_FAULT_COLLISION=2, REPORT_FAULT_TILT=3,
       REMOTE_LATCH_REASON_REMOTE_STOP=4, REMOTE_LATCH_REASON_OWNERSHIP_TRANSITION=5,
       EXECUTION_OWNER_MANUAL_REMOTE=0, GPIO_PIN_RESET=0, GPIO_PIN_10=10 };
#define GPIOD ((void *)0)
#define FLIPSKY_REALTIME_FEEDBACK_VALIDATED 0
#define FLIPSKY_CAN_RX_TIMEOUT_MS 100
static struct { unsigned g_system_report_state; } g_app;
static unsigned api_lockout, my_bms, pd10=1, reads;
static int HAL_GPIO_ReadPin(void *port, unsigned pin) {
    assert(port==GPIOD && pin==GPIO_PIN_10); ++reads; return pd10;
}
static bool Flipsky_CAN_ReadFeedback(FlipskyCanFeedback_t *out) { (void)out; return false; }
static bool AXEBMS_RS485_ReadTelemetry(unsigned *bms, AXEBMS_Data *out, uint32_t *at, bool *valid) {
    (void)bms; (void)out; (void)at; (void)valid; return false;
}
static bool chassis_control_read_wheel_feedback(ChassisWheelFeedback_t *out) { (void)out; return false; }
static bool SafetyOwnershipGate_IsOwnerPermitted(unsigned owner) { (void)owner; return false; }
'''
tests = r'''
int main(void) {
    RemoteCommand_t command={0};
    RemoteSafetyInput_t safety={0};
    TelemetrySnapshot_t snapshot;
    TelemetrySnapshot_Reset();
    for (unsigned fault=0; fault<2; ++fault) {
        safety.emergency_active=safety.collision_active=safety.tilt_active=!!fault;
        api_lockout=fault;
        for (unsigned sample=0; sample<6; ++sample) {
            pd10=sample%2;
            uint32_t now=1000U+sample*100U;
            RefreshTelemetrySnapshot(&command,&safety,false,now);
            assert(TelemetrySnapshot_Read(&snapshot));
            uint16_t word=snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION];
            assert((word&2U) && !!(word&1U)==!pd10);
            assert((word>>2)==now/100U);
            assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION]==0U);
            assert(snapshot.values[TELEMETRY_VALUE_WORK_MODE]==0U);
        }
    }
    assert(reads==12U);
    puts("PASS production PD10 source: low=connected, always sampled while locked/offline/in safety fault");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-bag-source-') as work:
    work = Path(work)
    cpp = work / 'source.c'
    cpp.write_text(fixture + '\n' + body + '\n' + tests)
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-I' + str(root / 'control_chassis/Core/Inc'),
        str(cpp), str(root / 'control_chassis/Core/UserApp/telemetry_snapshot.c'),
        '-o', str(work / 'source'),
    ], check=True)
    subprocess.run([str(work / 'source')], check=True)
