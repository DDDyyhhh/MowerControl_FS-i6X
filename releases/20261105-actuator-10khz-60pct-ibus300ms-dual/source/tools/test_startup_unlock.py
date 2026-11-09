#!/usr/bin/env python3
"""Closed loop: production GUI, both RF adapters, SENS, iBUS, Motion and authorization.

Only clock, receiver scheduling, GPIO/BMS values and physical output application
are fixtures. No HMI/controller generation or result is invented by this test.
"""
from pathlib import Path
import re, subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
p=root/'experiments/ticket03'; core=root/'control_chassis/Core'
view=(root/'OpenI6X/FS-I6X/opentx/radio/src/gui/128x64/view_main.cpp').read_text()
motion=(core/'UserApp/app_Motion.c').read_text()
def function(text,signature):
 a=text.index(signature); o=text.index('{',a);e=o+1;d=1
 while d: d+=(text[e]=='{')-(text[e]=='}');e+=1
 return text[a:e]
controller=r'''
#include "remote_backend.h"
#include "exp3_board.h"
#include "blade_manual_control.h"
#include "exp3_blade_adapter.h"
#include "battery_monitor.h"
#include "telemetry_snapshot.h"
#include "safety_ownership_gate.h"
#define RecordRemoteStop(...) ((void)0)
#include <assert.h>
#include <string.h>
#include <math.h>
extern uint32_t test_now;
static uint32_t clock_now(void *ctx){(void)ctx;return test_now;}
static struct {BatteryMonitor_t battery;unsigned lock,physical_safety_latched,cfg_collision_en,cfg_tilt_sensor_en;
 unsigned collision_reason_latched,emergency_reason_latched,g_collision_latched;
 unsigned g_system_report_state,receiver_status[16],knob_height;
 uint16_t Joystick[4];uint32_t last_receive_timestamp;bool communication_connected;} g_app;
static struct {float pitch,roll;} imu_data;
static unsigned last_host_cmd_tick;
#define REPORT_NONE 0U
#define STATUS_INDEX_BLADE_SPEED 0U
#define STATUS_INDEX_LOCK 1U
#define RGB_LED_SetColor(a,b) ((void)0)
#define API_TIMEOUT_MS 100U
#define GPIOD 0U
#define GPIO_PIN_0 0U
#define GPIO_PIN_RESET 0U
uint32_t HAL_GetTick(void);
static unsigned Ctrl_Mode;
static RemoteCommand_t command;
static RemoteSafetyInput_t safety;
static BladeManualControl_t g_blade_manual_control;
static Exp3PwmEvent g_motion_blade_event;
static Exp3PwmDiag g_exp3_pwm_diag;
static bool g_motion_blade_event_ready;
static bool VehicleEmergencyActive(void){return safety.emergency_active;}
static unsigned HAL_GPIO_ReadPin(unsigned port,unsigned pin){(void)port;(void)pin;return safety.collision_active?0U:1U;}
static void RefreshBatteryMonitor(uint32_t now){(void)now;}
static void ApplyBatteryAuthorization(void){SafetyOwnershipGate_SetBatteryPolicy(false,g_app.battery.blade_inhibited);}
static void RefreshTelemetrySnapshot(const RemoteCommand_t *c,const RemoteSafetyInput_t *s,bool a,uint32_t now){(void)c;(void)s;(void)a;(void)now;}
'''+function(motion,'static void RefreshRemoteIntent(')+'\n'+function(motion,'static void RecordUnlockDecision(')+'\n'+function(motion,'static void DispatchRemoteConfirmation(')+'\n'+function(motion,'static void RefreshSafetyOwnershipGate(void)\n{')+r'''
void test_controller_init(void){
 FlySkyIbusConfig_t cfg=RemoteBackend_DefaultFlySkyConfig();
 RemoteBackend_InitWithFlySkyConfig((RemoteBackendClock_t){clock_now,0},&cfg);
 BladeManualControl_Init(&g_blade_manual_control,0);g_app.lock=1U;
 TelemetrySnapshot_Reset();SafetyOwnershipGate_Reset();g_app.cfg_collision_en=1U;
}
void test_safety(unsigned value){safety.emergency_active=value==1U;safety.collision_active=value==2U;
 if(value){safety.physical_latched=true;g_app.physical_safety_latched=g_app.lock=1U;}}
void test_battery(unsigned low){g_app.battery.blade_inhibited=low!=0U;}
unsigned test_permitted(void){return command.run_permitted;}
unsigned test_timeout_history(void){return command.timeout_latched;}
unsigned test_faults(void){TelemetrySnapshot_t s;assert(TelemetrySnapshot_Read(&s));return s.values[TELEMETRY_VALUE_DETAILED_FAULT];}
unsigned test_blade(void){return BladeManualControl_GetState(&g_blade_manual_control)==BLADE_STATE_RUNNING;}
void test_mode(unsigned mode){Ctrl_Mode=mode;}
void test_reboot(void){test_controller_init();exp3_board_restart_session(test_now);}
void test_receive(uint16_t ch4,uint32_t packed){
 uint16_t channels[14];uint8_t bytes[32]={32,64};uint16_t sum=0U;
 for(unsigned i=0;i<14U;i++)channels[i]=1500U;
 channels[3]=ch4;channels[4]=2000U;channels[5]=1000U;channels[6]=2000U;
 channels[10]=packed?(uint16_t)packed:1500U;
 channels[11]=packed?(uint16_t)(packed>>16):1500U;
 for(unsigned i=0;i<14U;i++){bytes[2+i*2]=(uint8_t)channels[i];bytes[3+i*2]=(uint8_t)(channels[i]>>8);}
 for(unsigned i=0;i<30U;i++)sum+=bytes[i];sum=0xffffU-sum;
 bytes[30]=(uint8_t)sum;bytes[31]=(uint8_t)(sum>>8);
 RemoteBackend_OnUartRx(1U,bytes,32U);
}
void test_controller_tick(void){
 RemoteBackend_Advance(test_now,safety);RemoteBackend_GetCommand(&command);
 safety.physical_latched=g_app.physical_safety_latched!=0U;
 RefreshSafetyOwnershipGate();RemoteBackend_GetCommand(&command);
 BladeManualControlInput_t in={0};
 in.link_fresh=command.link_fresh;in.timeout_latched=command.timeout_latched;
 in.remote_run_enable=command.remote_run_enable;in.run_permitted=command.run_permitted;
 in.software_locked=g_app.lock;in.physical_latched=safety.physical_latched;
 in.emergency_active=safety.emergency_active;in.collision_active=safety.collision_active;
 in.selected_owner=command.run_permitted?EXECUTION_OWNER_MANUAL_REMOTE:EXECUTION_OWNER_NONE;
 in.ch4_raw=command.blade_start_raw;in.ch7_raw=command.blade_gear_raw;
 in.confirmed_gear=(RemoteBladeGear_t)command.blade_gear;in.frame_sequence=command.frame_sequence;
 in.command_invalidation_sequence=command.command_invalidation_sequence;
 bool ready=g_motion_blade_event_ready, injected=false;Exp3PwmEvent event=g_motion_blade_event;
 if(ready){
  g_motion_blade_event_ready=false;
  Exp3BladeInput bridge={0};bridge.event=event;bridge.blade_state=g_blade_manual_control.state;
  bridge.ch4_raw=in.ch4_raw;bridge.ch4_epoch=exp3_board_ch4_epoch();
  bridge.request_sequence=g_blade_manual_control.request_sequence;
  bridge.baseline_event_sequence=g_blade_manual_control.baseline_event_sequence;
  bridge.last_consumed_event_sequence=g_blade_manual_control.last_consumed_event_sequence;
  bridge.gate_permitted=command.run_permitted&&!safety.physical_latched;
  Exp3BladeDecision d=exp3_blade_adapter_decide(&bridge);
  if(d.inject_confirm){BladeConfirmOK_t ok={.version=BLADE_PROTOCOL_VERSION,
   .request_sequence=d.request_sequence,.event_sequence=d.event_sequence};
   BladeManualControl_InjectConfirmOK(&g_blade_manual_control,&ok);injected=true;}
  in.command_invalidated=d.invalidate;
 }
 if(g_app.battery.blade_inhibited)BladeManualControl_Reset(&g_blade_manual_control);
 else BladeManualControl_Step(&g_blade_manual_control,&in);
 if(ready)exp3_board_complete_pwm_event(&event,event.kind==EXP3_PWM_CANCEL ||
  (injected&&g_blade_manual_control.state==BLADE_STATE_RUNNING));
 RefreshRemoteIntent(test_now,&command,safety);
 TelemetrySources_t src={0};src.manual_remote_available=command.link_fresh&&command.stable_online;
 src.final_manual_run_permitted=command.run_permitted&&!g_app.lock;
 src.system_locked=g_app.lock||command.remote_recoverable_latched||safety.physical_latched;
 src.emergency_active=safety.emergency_active;src.collision_active_or_latched=safety.collision_active;
 src.ibus_timeout_latched=command.timeout_latched;
 src.blade_output_bits=test_blade()?3U:0U;src.authorization=g_exp3_hil.authority;
 TelemetrySnapshot_Update(&src,test_now);
}
'''
# Exercise the production input/telemetry mappings rather than a copied policy.
blade_timeout=re.search(r'blade_input\.timeout_latched = (.*);',motion)[1].replace('rc_cmd','command')
fault_timeout=re.search(r'sources\.ibus_timeout_latched = (.*);',motion)[1].replace('command->','command.').replace('(command)','(&command)')
controller=controller.replace('in.timeout_latched=command.timeout_latched;', 'in.timeout_latched='+blade_timeout+';')
controller=controller.replace('src.ibus_timeout_latched=command.timeout_latched;', 'src.ibus_timeout_latched='+fault_timeout+';')
# Enable the same two production ApplyCommand blocks as the dedicated physical
# recovery regression. Board hardware includes/ISR controls remain fakes.
backend=(core/'UserDev/remote_backend.c').read_text()
for original in ['#if !defined(REMOTE_BACKEND_HOST)\nstatic void RemoteBackend_ApplyCommand(void)',
                 '#if !defined(REMOTE_BACKEND_HOST)\n    g_app.communication_connected = g_command.connected;']:
 assert backend.count(original)==1
 backend=backend.replace(original,original.replace('#if !defined(REMOTE_BACKEND_HOST)','#if 1'))
backend=backend.replace('"../../../experiments/ticket03/exp3.h"','"'+str(p/'exp3.h')+'"')
controller=controller.replace('static bool VehicleEmergencyActive(',backend+'\nstatic bool VehicleEmergencyActive(',1)
ui_prefix=r'''
#include "exp3_board.h"
extern "C" {
#include "sens_ibus_responder.h"
}
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
using event_t=int;
enum{KEY_ENTER=0};
#define EVT_KEY_BREAK(x) (0x20|(x))
static unsigned ticks;
static unsigned get_tmr10ms(){return ticks;}
static int channelOutputs[14];
#define PPM_CH_CENTER(x) 1500
static const unsigned rssi=100;
template<typename T>T limit(T lo,T v,T hi){return v<lo?lo:v>hi?hi:v;}
extern "C" {
uint32_t test_now;
uint32_t HAL_GetTick(void){return test_now;}
uint32_t HAL_GetUIDw0(void){return 123U;}
uint32_t HAL_GetUIDw1(void){return 456U;}
struct FakeSysTick{uint32_t VAL;};struct FakeSysTick fake_systick={7U};
void test_controller_init(void);void test_controller_tick(void);void test_receive(uint16_t,uint32_t);
void test_safety(unsigned);void test_battery(unsigned);void test_mode(unsigned);void test_reboot(void);
unsigned test_permitted(void);unsigned test_blade(void);
unsigned test_timeout_history(void);unsigned test_faults(void);
extern Exp3Hil controller_g_exp3_hil;
void controller_exp3_board_poll(uint32_t);
uint32_t controller_exp3_board_completion_part(unsigned);
}
'''
ui_declarations=view[view.index('static Exp3UiProbe mowerExp3Probe;'):view.index('static Exp3UiState mowerExp3Step(')]
ui=ui_prefix+'\n'+ui_declarations+'\n'+function(view,'static Exp3UiState mowerExp3Step(')+'\n'+(p/'test_startup_cycle.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='mower-startup-v35-') as directory:
 w=Path(directory);(w/'main.h').write_text('''#include <stdint.h>
uint32_t HAL_GetTick(void);uint32_t HAL_GetUIDw0(void);uint32_t HAL_GetUIDw1(void);
struct FakeSysTick{uint32_t VAL;};extern struct FakeSysTick fake_systick;
#define SysTick (&fake_systick)
''')
 flags=['-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-misleading-indentation',
 '-fsanitize=address,undefined','-DEXP3_ENABLED=1','-DEXP3_PWM_PROBE_ENABLED=1','-DEXP3_AUTO_SESSION_ENABLED=1',
 '-DMOWER_BLADE_OK_ENABLED=1','-DREMOTE_BACKEND_HOST','-DREMOTE_BACKEND_A28_E34=0','-DREMOTE_BACKEND_FLYSKY_IBUS=1',
 '-I'+str(w),'-I'+str(p),'-I'+str(core/'Inc')]
 names=['g_exp3_hil','g_exp3_mailbox','g_exp3_downlink_word','g_exp3_capture_overflow']
 for file in ['stm32_adapter.c','exp3_hil.c']:
  names+=re.findall(r'^(?:void|bool|uint32_t|uint16_t)\s+(exp3_\w+)\(', (p/file).read_text(),re.M)
 aliases=['-D'+n+'=controller_'+n for n in set(names)]
 (w/'motion.c').write_text(controller);(w/'ui.cpp').write_text(ui)
 objects=[]
 files=[(w/'motion.c',aliases),(p/'stm32_adapter.c',aliases),(p/'exp3_hil.c',aliases),
 (core/'UserDev/sens_ibus_responder.c',aliases)]
 files += [(p/n,[]) for n in ['exp3.c','exp3_link.c','exp3_hil.c','exp3_auto_session.c','exp3_probe.c','openi6x_adapter.c','exp3_blade_adapter.c']]
 files += [(core/'UserDev'/n,aliases) for n in ['remote_authorization.c','flysky_ibus_servo.c','blade_confirm_ch6_adapter.c']]
 files += [(core/'UserApp'/n,[]) for n in ['telemetry_snapshot.c','blade_manual_control.c','safety_ownership_gate.c']]
 for i,(file,extra) in enumerate(files):
  obj=w/(str(i)+'.o');subprocess.run(['cc','-std=c99',*flags,*extra,'-c',str(file),'-o',str(obj)],check=True);objects.append(obj)
 subprocess.run(['c++','-std=c++11',*flags,'-DPCBI6X_MOWER_BLADE_OK=1',str(w/'ui.cpp'),*map(str,objects),'-lm','-o',str(w/'test')],check=True)
 for scenario in os.environ.get('MOWER_STARTUP_SCENARIOS',','.join(map(str,range(15)))).split(','):
  subprocess.run([str(w/'test'),scenario],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
