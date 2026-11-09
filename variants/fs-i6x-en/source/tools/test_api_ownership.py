"""Run the production RefreshSafetyOwnershipGate call site against the real gate."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'control_chassis/Core/UserApp/app_Motion.c').read_text()
signature = 'static void RefreshSafetyOwnershipGate(void)\n{'
begin = source.index(signature)
brace = source.index('{', begin)
depth = 1
end = brace+1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[begin:end]
def function(text, signature):
    a=text.index(signature); b=text.index('{',a); e=b+1; depth=1
    while depth:
        depth+=(text[e]=='{')-(text[e]=='}'); e+=1
    return text[a:e]
record = function(source,'static void RecordRemoteStop(')
boot = function((ROOT/'control_chassis/Core/UserApp/app_context.c').read_text(),
                'void AppContext_RecordBootReset(')
header = (ROOT/'control_chassis/Core/Inc/chassis_control.h').read_text()
timeout = re.search(r'#define\s+API_TIMEOUT_MS\s+(\d+)', header).group(1)
fixture = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "safety_ownership_gate.h"
#define REMOTE_BACKEND_A28_E34 0
#define REMOTE_BACKEND_FLYSKY_IBUS 1
#include "remote_stop_diagnostics.h"
static struct {
 uint8_t physical_safety_latched,cfg_collision_en,cfg_tilt_sensor_en,lock,g_collision_latched;
 unsigned g_system_report_state,knob_height;
 RemoteStopDiagnostics_t remote_stop_diag;
} g_app;
static struct { float pitch, roll; } imu_data;
static RemoteCommand_t remote;
static bool available;
static uint32_t now_ms_fake, last_host_cmd_tick;
static uint8_t Ctrl_Mode;
static struct { uint32_t IDR; } gpio;
#define GPIOD (&gpio)
#define __DMB() ((void)0)
static struct { unsigned ccr3,ccr4,period; } htim3;
#define TIM_CHANNEL_3 3U
#define TIM_CHANNEL_4 4U
#define __HAL_TIM_GET_COMPARE(t,c) ((c)==3U?(t)->ccr3:(t)->ccr4)
#define __HAL_TIM_GET_AUTORELOAD(t) ((t)->period)
#define GPIO_PIN_0 0
#define GPIO_PIN_RESET 0
static uint32_t HAL_GetTick(void) { return now_ms_fake; }
static bool VehicleEmergencyActive(void) { return false; }
static int HAL_GPIO_ReadPin(void *port, int pin) { (void)port; (void)pin; return 1; }
void RemoteBackend_Advance(uint32_t now, RemoteSafetyInput_t safety) { (void)now; (void)safety; }
bool RemoteBackend_GetCommand(RemoteCommand_t *out) { *out=remote; return available; }
void RemoteBackend_GetRxDiagnostics(RemoteRxDiagnostics_t *out) { memset(out,0,sizeof(*out)); }
static void RefreshBatteryMonitor(uint32_t now) { (void)now; }
static void ApplyBatteryAuthorization(void) {}
static void RefreshTelemetrySnapshot(const RemoteCommand_t *command, const RemoteSafetyInput_t *safety,
                                    bool present, uint32_t now) {
 (void)command; (void)safety; (void)present; (void)now;
}
'''
tests = r'''
static void ResetFixture(uint8_t mode) {
 memset(&g_app,0,sizeof(g_app)); memset(&remote,0,sizeof(remote));
 AppContext_RecordBootReset(0U);
 remote.run_permitted=true; remote.connected=true; remote.link_fresh=true; remote.stable_online=true;
 remote.frame_sequence=1; available=true; now_ms_fake=1000; last_host_cmd_tick=1000; Ctrl_Mode=mode;
 SafetyOwnershipGate_Reset();
}
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr,"FAIL line %d: %s; API reason=%d\n",__LINE__,#expr,(int)SafetyOwnershipGate_GetReason(EXECUTION_OWNER_API)); return 1; } } while(0)
int main(void) {
 ResetFixture(0); RefreshSafetyOwnershipGate();
 CHECK(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 CHECK(g_app.remote_stop_diag.armed && !g_app.remote_stop_diag.valid);
 remote.link_fresh=false; remote.run_permitted=false;
 remote.remote_recoverable_latched=true; remote.recoverable_latch_reason=REMOTE_LATCH_REASON_TIMEOUT;
 now_ms_fake+=100U; RefreshSafetyOwnershipGate();
 CHECK(g_app.remote_stop_diag.valid && g_app.remote_stop_diag.event_count==1U);
 CHECK(g_app.remote_stop_diag.gate_reason==SAFETY_GATE_REJECT_REMOTE_LATCH);
 CHECK(g_app.remote_stop_diag.latch_reason==REMOTE_LATCH_REASON_TIMEOUT);
 CHECK(g_app.remote_stop_diag.input_flags & (1U<<4));
 g_app.physical_safety_latched=1U; now_ms_fake+=200U; RefreshSafetyOwnershipGate();
 CHECK(g_app.remote_stop_diag.event_count==1U && g_app.remote_stop_diag.captured_ms==1100U);
 ResetFixture(2); RefreshSafetyOwnershipGate();
 CHECK(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 ResetFixture(1); RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 ResetFixture(0); RefreshSafetyOwnershipGate();
 Ctrl_Mode=2; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 last_host_cmd_tick=1050; now_ms_fake=1050; RefreshSafetyOwnershipGate();
 CHECK(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 Ctrl_Mode=0; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 remote.frame_sequence++; RefreshSafetyOwnershipGate();
 CHECK(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 ResetFixture(2); now_ms_fake=1000+API_TIMEOUT_MS; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); g_app.lock=1; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); g_app.physical_safety_latched=1; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); remote.remote_recoverable_latched=true; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); remote.run_permitted=false; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); remote.link_fresh=false; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); remote.stable_online=false; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); available=false; RefreshSafetyOwnershipGate();
 CHECK(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ResetFixture(2); now_ms_fake=5; last_host_cmd_tick=UINT32_MAX-10; RefreshSafetyOwnershipGate();
 CHECK(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 puts("PASS production API ownership, standby, freshness, remote stop and all safety locks");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='mower_api_gate_') as directory:
    harness = Path(directory)/'callsite.c'
    executable = Path(directory)/'callsite'
    harness.write_text(f'#define API_TIMEOUT_MS {timeout}U\n'+fixture+'\n'+boot+'\n'+record+'\n'+body+'\n'+tests)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'control_chassis/Core/Inc'), str(harness),
                    str(ROOT/'control_chassis/Core/UserApp/safety_ownership_gate.c'),
                    '-lm', '-o', str(executable)], check=True)
    raise SystemExit(subprocess.run([str(executable)]).returncode)
