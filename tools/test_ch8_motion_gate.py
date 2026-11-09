#!/usr/bin/env python3
"""Verify the actual production Motion arbitration and stop call sites."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'control_chassis/Core/UserApp/app_Motion.c').read_text()
def function(signature):
    start=source.index(signature+'\n{')
    end=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mower_mode_control.h"
#include "safety_ownership_gate.h"
#include "exp3_board.h"
#define API_TIMEOUT_MS 500U
#define GPIOD ((void *)0)
#define GPIO_PIN_0 0
#define GPIO_PIN_RESET 0
#define STATUS_INDEX_BLADE_SPEED 1
typedef struct { bool physical,emergency,collision,tilt; } RemoteSafetyInput_t;
typedef struct {
 bool run_permitted,remote_recoverable_latched,connected,link_fresh,stable_online,mode_valid;
 uint8_t selected_mode; uint32_t frame_sequence,command_invalidation_sequence;
} RemoteCommand_t;
static struct {
 uint8_t physical_safety_latched,cfg_collision_en,cfg_tilt_sensor_en,lock,knob_blade,receiver_status[2],trigger_blade_init;
 uint16_t Joystick[4]; float current_ramped_speed,speed;
} g_app;
static struct { float pitch,roll; } imu_data;
static uint32_t g_mower_stop_sequence;
static uint32_t now_ms=1000U,last_host_cmd_tick=1000U,host_sequence=1U;
static uint8_t Ctrl_Mode;
static MowerModeControl_t mode;
static RemoteCommand_t remote;
static unsigned stopped,cleared,height_stopped,bag_stopped,completions;
static bool blade_pending,completed_accepted;
static Exp3PwmEventKind blade_event_kind;
static int g_blade_manual_control;
volatile uint16_t g_mower_unlock_generation,g_mower_control_word;
static uint32_t HAL_GetTick(void) { return now_ms; }
static bool VehicleEmergencyActive(void) { return false; }
static int HAL_GPIO_ReadPin(void *port,int pin) { (void)port; (void)pin; return 1; }
static MowerModeControl_t *RemoteBackend_ModeControl(void) { return &mode; }
static void exp3_board_status_fake(Exp3BoardStatus *out) {
 memset(out,0,sizeof(*out)); out->controller=1; out->hmi=2; out->bound=1;
}
#define exp3_board_status exp3_board_status_fake
static void RemoteBackend_Advance(uint32_t now,RemoteSafetyInput_t safety) {
 (void)now; (void)safety;
 remote.run_permitted=mode.permitted; remote.remote_recoverable_latched=!mode.armed;
 remote.mode_valid=mode.mode_valid; remote.selected_mode=(uint8_t)mode.selected;
 remote.command_invalidation_sequence=mode.invalidation_sequence;
 g_app.lock=!mode.permitted;
}
static bool RemoteBackend_GetCommand(RemoteCommand_t *out) { *out=remote; return true; }
static bool exp3_board_take_pwm_event_fake(Exp3PwmEvent *event) {
 if (!blade_pending) return false;
 blade_pending=false; memset(event,0,sizeof(*event)); event->kind=blade_event_kind; return true;
}
#define exp3_board_take_pwm_event exp3_board_take_pwm_event_fake
static bool exp3_board_take_chassis_event_fake(Exp3PwmEvent *event) { (void)event; return false; }
#define exp3_board_take_chassis_event exp3_board_take_chassis_event_fake
static void exp3_board_complete_pwm_event_fake(const Exp3PwmEvent *event,bool accepted) { (void)event; completed_accepted=accepted; ++completions; }
#define exp3_board_complete_pwm_event exp3_board_complete_pwm_event_fake
static void exp3_board_request_rebind_fake(void) {}
#define exp3_board_request_rebind exp3_board_request_rebind_fake
static void chassis_control_discard_host(void) {}
static void chassis_control_advance_host(void) {}
static uint32_t chassis_control_host_sequence(void) { return host_sequence; }
static void chassis_control_clear_cached_intents(void) { ++cleared; }
static void chassis_stop(void) { ++stopped; }
static void BladeMotor_SetSpeed(float speed) { assert(speed==0); }
static void Cruise_Stop(void) {}
static void Lifter_SetHeightPermission(bool permitted) { assert(!permitted); ++height_stopped; }
static void Lifter_SetGrassBagPermission(bool permitted) { assert(!permitted); ++bag_stopped; }
static void BladeManualControl_Reset(int *state) { (void)state; }
static void RefreshTelemetrySnapshot(const RemoteCommand_t *r,const RemoteSafetyInput_t *s,bool present,uint32_t now) {
 (void)r; (void)s; (void)present; (void)now;
}
static void reset(uint8_t selected,uint8_t api_mode) {
 memset(&g_app,0,sizeof(g_app)); memset(&remote,0,sizeof(remote));
 MowerMode_Init(&mode); MowerMode_Session(&mode,1,2);
 mode.selected=(MowerSelectedMode_t)selected; mode.api_mode=api_mode;
 mode.mode_valid=mode.armed=mode.permitted=true; mode.stop_requested=false;
 remote.connected=remote.link_fresh=remote.stable_online=true; remote.frame_sequence=1;
 now_ms=last_host_cmd_tick=1000; host_sequence=1;
 stopped=cleared=height_stopped=bag_stopped=completions=0; blade_pending=false;
 SafetyOwnershipGate_Reset();
}
'''
tests=r'''
int main(void) {
 reset(MOWER_SELECT_RC,0); RefreshSafetyOwnershipGate();
 assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,0); RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,1); RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,2); RefreshSafetyOwnershipGate();
 assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 blade_pending=true; blade_event_kind=EXP3_PWM_CONFIRM; RefreshSafetyOwnershipGate();
 assert(completions==1 && !completed_accepted && !blade_pending);
 blade_pending=true; blade_event_kind=EXP3_PWM_CANCEL; RefreshSafetyOwnershipGate();
 assert(completions==2 && completed_accepted && !blade_pending);
 reset(MOWER_SELECT_RC,2); RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,2); now_ms=1500; RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,2); g_app.physical_safety_latched=1; RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,2); remote.link_fresh=false; RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_API,2); mode.armed=mode.permitted=false; RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 reset(MOWER_SELECT_RC,0); RefreshSafetyOwnershipGate();
 mode.selected=MOWER_SELECT_API; mode.api_mode=2; mode.stop_requested=true;
 RefreshSafetyOwnershipGate(); EnforceSafetyOwnershipStop();
 assert(stopped==1 && cleared==1 && height_stopped==1 && bag_stopped==1 && mode.armed);
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 ++remote.frame_sequence; RefreshSafetyOwnershipGate();
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true)); // RC frames cannot freshen API intent.
 ++host_sequence; last_host_cmd_tick=++now_ms; RefreshSafetyOwnershipGate();
 assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API,true));
 puts("PASS actual production Motion gate, selected source, standby/freshness, safety and complete handoff stop");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-motion-') as directory:
    directory=Path(directory)
    harness=directory/'motion.c'
    harness.write_text(fixture+function('static void RefreshSafetyOwnershipGate(void)')+'\n'+
                       function('static void EnforceSafetyOwnershipStop(void)')+tests)
    exe=directory/'motion'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-DMOWER_BLADE_OK_ENABLED=1','-DEXP3_ENABLED=1','-DEXP3_PWM_PROBE_ENABLED=1',
                    '-DREMOTE_BACKEND_A28_E34=0','-DREMOTE_BACKEND_FLYSKY_IBUS=1',
                    '-I'+str(root/'control_chassis/Core/Inc'),'-I'+str(root/'experiments/ticket03'),str(harness),
                    str(root/'control_chassis/Core/UserDev/mower_mode_control.c'),
                    str(root/'control_chassis/Core/UserApp/safety_ownership_gate.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
