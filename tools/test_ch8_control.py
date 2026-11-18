#!/usr/bin/env python3
"""Exercise the selector and actual Motion/CAN/actuator handoff entry points."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from _harness_util import c_function
ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'control_chassis/Core'
motion = (CORE/'UserApp/app_Motion.c').read_text()
chassis = (CORE/'UserDev/chassis_control.c').read_text()
blade_header = (CORE/'Inc/BladeMotor.h').read_text()
timeout = int(re.search(r'#define\s+API_TIMEOUT_MS\s+(\d+)', (CORE/'Inc/chassis_control.h').read_text())[1])
fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "remote_backend.h"
#include "control_source.h"
#include "safety_ownership_gate.h"
#include "blade_motor_config.h"
static struct { unsigned lock,physical_safety_latched,cfg_collision_en,cfg_tilt_sensor_en;
 uint16_t Joystick[4]; unsigned knob_blade,knob_height,receiver_status[16],trigger_blade_init;
 float current_ramped_speed,speed; } g_app;
static struct {float pitch,roll;} imu_data;
static RemoteCommand_t remote;
static ControlSource_t g_control_source;
static HostControlSnapshot_t g_host_control,g_host_snapshot;
static GrassBagRemote_t bag_remote;
static uint32_t now_ms,last_host_cmd_tick;
static uint8_t Ctrl_Mode,api_lockout;
static float host_vx,host_vy,host_omega;
static uint16_t g_blade_ok_runtime_word;
static int g_blade_manual_control;
static unsigned critical_depth,stops,blade_resets,height_calls,bag_calls,cancelled,preempt_ms;
static unsigned height_target,bag_target;
static bool height_permission,bag_permission,available=true,emergency,bag_connected=true;
static float blade_output;
#define GPIOD ((void *)0)
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_0 0U
#define GPIO_PIN_10 10U
#define STATUS_INDEX_BLADE_SPEED 0U
#define HOST_CMD_FRAME_ID 5U
#define taskENTER_CRITICAL() (++critical_depth)
#define taskEXIT_CRITICAL() do { --critical_depth; now_ms+=preempt_ms; preempt_ms=0; } while (0)
#define RecordRemoteStop(...) ((void)0)
static uint32_t HAL_GetTick(void) { return now_ms; }
static bool VehicleEmergencyActive(void) { return emergency; }
static unsigned HAL_GPIO_ReadPin(void *port,unsigned pin) { (void)port; return pin==10U ? !bag_connected : 1U; }
void RemoteBackend_Advance(uint32_t now,RemoteSafetyInput_t safety) { (void)now;(void)safety; }
bool RemoteBackend_GetCommand(RemoteCommand_t *out) { *out=remote;return available; }
void RemoteBackend_CancelUnlock(void) { ++cancelled; }
void RemoteBackend_RevokeAuthorization(void) { remote.run_permitted=false;g_app.lock=1; }
bool RemoteBackend_AdvanceGrassBag(bool allowed,GrassBagTarget_t *target) {
 return GrassBagRemote_Advance(&bag_remote,remote.grass_bag_raw,remote.frame_sequence,allowed,target);
}
static void RefreshBatteryMonitor(uint32_t now) { (void)now; }
static void ApplyBatteryAuthorization(void) {}
static void RefreshRemoteIntent(uint32_t n,const RemoteCommand_t *c,RemoteSafetyInput_t s) {(void)n;(void)c;(void)s;}
static void DispatchRemoteConfirmation(uint32_t n,RemoteSafetyInput_t s,RemoteCommand_t *c) {(void)n;(void)c;(void)s;}
static void RefreshTelemetrySnapshot(const RemoteCommand_t *c,const RemoteSafetyInput_t *s,bool a,uint32_t n) {
 (void)c;(void)s;(void)a;(void)n;
}
static void Lifter_SetHeightPermission(bool allowed) {height_permission=allowed;if(!allowed)height_target=0;}
static void Lifter_SetGrassBagPermission(bool allowed) {bag_permission=allowed;if(!allowed)bag_target=0;}
static void Lifter_SetTargetLevel(unsigned value) {assert(height_permission);height_target=value;++height_calls;}
static void Lifter_SetGrassBagTarget(unsigned value) {assert(bag_permission);bag_target=value;++bag_calls;}
static void chassis_stop(void) {++stops;}
static void BladeMotor_SetSpeed(float value) { assert(value==0);blade_output=0; }
static void BladeMotor_SetSpeedForOwner(ExecutionOwner_t owner,float value) {
 blade_output=SafetyOwnershipGate_AuthorizeBlade(owner,value!=0)?value:0;
}
static void Cruise_Stop(void) {}
static void BladeManualControl_Reset(int *p) {(void)p;++blade_resets;}
'''
functions = '\n'.join(c_function(chassis, sig) for sig in [
    'void chassis_control_parse_host_frame(', 'void chassis_control_read_host_snapshot(',
    'void chassis_control_apply_host_snapshot(', 'void chassis_control_clear_cached_intents('])
functions += '\n'+c_function(blade_header,'static inline float BladeMotor_CommandFromGear(')
functions += '\n'+'\n'.join(c_function(motion,sig) for sig in [
    'static void RefreshControlSource(',
    'static void RefreshSafetyOwnershipGate(void)\n{',
    'static void EnforceSafetyOwnershipStop(void)\n{',
    'static void ApplyControlSourceActuators(void)\n{'])
a = motion.index('#if MOWER_CH8_CONTROL_ENABLED\n          if (Ctrl_Mode == CONTROL_MODE_API_RUN')
b = motion.index('#if EXP3_PWM_PROBE_ENABLED',a)
functions += '\nstatic void ApplyApiBlade(void) { float blade_command=0;\n'+motion[a:b]+'\n}\n'
cases = r'''
static void tick(uint32_t dt) {
 now_ms+=dt;remote.last_valid_ms=now_ms;++remote.frame_sequence;
 RefreshSafetyOwnershipGate();EnforceSafetyOwnershipStop();ApplyControlSourceActuators();ApplyApiBlade();
 assert(critical_depth==0);
}
static void host(unsigned mode) {
 uint8_t p[8]={(uint8_t)mode,5,0,2,3,4,1,0xAA};
 chassis_control_parse_host_frame(5,p,8,now_ms);
}
static void stopped(void) {assert(!height_target && !bag_target && !blade_output && !host_vx && !host_omega);}
static void select(unsigned ch8) {remote.channels[7]=ch8;for(unsigned n=0;n<3;++n)tick(10);}
static void reset(void) {
 g_app=(__typeof__(g_app)){0};remote=(RemoteCommand_t){0};
 remote.run_permitted=remote.link_fresh=remote.connected=remote.stable_online=true;
 remote.channels[0]=remote.channels[1]=1500;remote.height_level=3;remote.grass_bag_raw=1000;
 g_host_control=(HostControlSnapshot_t){0};g_host_snapshot=g_host_control;
 ControlSource_Reset(&g_control_source);SafetyOwnershipGate_Reset();bag_remote=(GrassBagRemote_t){0};
 now_ms=1000;Ctrl_Mode=api_lockout=0;available=true;bag_connected=true;
 host_vx=host_vy=host_omega=blade_output=0;height_target=bag_target=height_calls=bag_calls=0;
 select(1000);for(unsigned n=0;n<25;++n)tick(10);
 assert(SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_MANUAL_REMOTE));
}
static void run(void) {
 select(2000);stopped();assert(Ctrl_Mode==1);
 host(2);tick(10);assert(Ctrl_Mode==1);stopped();
 host(1);tick(10);host(2);tick(10);assert(Ctrl_Mode==2);stopped();
 for(unsigned n=0;n<3;++n){host(2);tick(10);}
 assert(SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_API));
 assert(host_vx==0.5f && host_omega==0.2f && height_target==4 && bag_target==1);
 assert(blade_output==BladeMotor_CommandFromGear(3));
}
int main(void) {
 assert(API_TIMEOUT_MS==CONTROL_API_TIMEOUT_MS);
 reset();host(1);tick(10);host(2);tick(10);assert(Ctrl_Mode==0);stopped();
 run();remote.channels[1]=1551;tick(10);stopped();assert(Ctrl_Mode==0 && api_lockout && remote.run_permitted);
 host(1);tick(10);host(2);tick(10);stopped();assert(Ctrl_Mode==0);
 remote.channels[1]=1500;for(unsigned n=0;n<25;++n)tick(10);
 assert(g_control_source.remote_ready && !height_target && !bag_target);
 unsigned before=height_calls;remote.height_level=2;tick(10);assert(height_calls==before+1 && height_target==2);
 select(1000);run();host(0);tick(10);assert(Ctrl_Mode==1);stopped();
 select(1000);for(unsigned n=0;n<25;++n)tick(10);run();
 now_ms=g_host_snapshot.received_at_ms+API_TIMEOUT_MS;tick(0);stopped();
 assert(!remote.run_permitted && g_app.lock && api_lockout);
 host(1);tick(10);host(2);tick(10);assert(!remote.run_permitted);stopped();
 for(unsigned fault=0;fault<6;++fault) {
   reset();run();
   if(fault==0)g_app.physical_safety_latched=1;
   if(fault==1)g_app.lock=1;
   if(fault==2)remote.remote_recoverable_latched=true;
   if(fault==3)remote.link_fresh=false;
   if(fault==4)remote.stable_online=false;
   if(fault==5)available=false;
   tick(10);stopped();assert(!SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_API));
 }
 reset();run();host(2);
 /* CANRx may publish a newer timestamp after Motion first samples the clock. */
 g_host_control.received_at_ms=now_ms+5;preempt_ms=5;tick(0);
 assert(SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_API));
 assert(height_permission && bag_permission && host_vx==0.5f);
 reset();run();remote.control_source_events+=2;tick(10);stopped(); /* lost excursion */
 reset();run();bag_connected=false;tick(10);assert(!bag_permission && !bag_target);
 uint32_t sequence=g_host_control.sequence;
 uint8_t p[8]={1};chassis_control_parse_host_frame(0x105,p,8,now_ms);
 chassis_control_parse_host_frame(5,p,7,now_ms);p[0]=3;chassis_control_parse_host_frame(5,p,8,now_ms);
 assert(g_host_control.sequence==sequence);
 puts("PASS actual Motion/CAN handoff: full API actuators, immediate stops, preserved grant, recenter, source fences and fault gates");
}
'''
flags=['-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
       '-DREMOTE_BACKEND_A28_E34=0','-DREMOTE_BACKEND_FLYSKY_IBUS=1','-DMOWER_BLADE_OK_ENABLED=1',
       '-DMOWER_GRASS_BAG_CH6_ENABLED=1','-I'+str(CORE/'Inc')]
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
with tempfile.TemporaryDirectory(prefix='mower-ch8-') as folder:
    out=Path(folder)
    for optimization in ('-O0','-O2'):
        for name,src,dependencies in [
            ('selector',ROOT/'control_chassis/host/control_source_harness.c',[]),
            ('motion',out/'motion.c',[CORE/'UserApp/safety_ownership_gate.c',CORE/'UserDev/grass_bag_control.c'])]:
            if name=='motion': src.write_text(f'#define API_TIMEOUT_MS {timeout}U\n'+fixture+functions+cases)
            for driver in ((1,2) if name=='motion' else (2,)):
                binary=out/f'{name}-{driver}'
                subprocess.run([os.environ.get('CC','cc'),*flags,optimization,f'-DBLADE_MOTOR_DRIVER_USED={driver}',
                    str(src),str(CORE/'UserApp/control_source.c'),*map(str,dependencies),'-lm','-o',str(binary)],check=True)
                subprocess.run([str(binary)],env=env,check=True)
