#!/usr/bin/env python3
"""Run real BMS publication, Motion battery hooks and all final wheel adapters."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'control_chassis/Core'


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined']
gate = str(CORE / 'UserApp/safety_ownership_gate.c')
monitor = str(CORE / 'UserApp/battery_monitor.c')

with tempfile.TemporaryDirectory(prefix='mower-battery-') as directory:
    work = Path(directory)

    def run(sources, extra=()):
        exe = str(work / 'test')
        subprocess.run(['cc', *flags, '-I'+str(work), '-I'+str(CORE/'Inc'),
                        *extra, *map(str, sources), '-lm', '-o', exe], check=True)
        subprocess.run([exe], check=True, timeout=5, env=env)

    run([ROOT/'control_chassis/host/battery_monitor_harness.c', monitor, gate])
    (work/'main.h').write_text('#include <stdint.h>\nuint32_t HAL_GetTick(void);\n#define __DMB() ((void)0)\n')
    (work/'rs485.h').write_text('''#include <stdint.h>
void RS485_SendData(uint8_t *,uint16_t);
uint16_t RS485_ReceiveData(uint8_t *,uint16_t);
''')
    prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "battery_monitor.h"
#include "AXEBMS_RS485.h"
#include "modbus_rtu_bus.h"
#include "safety_ownership_gate.h"
#include "../../../experiments/ticket03/exp3_probe.h"
static Exp3PwmEvent g_motion_blade_event;
static bool g_motion_blade_event_ready, completion_accepted;
static unsigned completions;
static void exp3_board_complete_pwm_event(const Exp3PwmEvent *event,bool accepted) {
 (void)event; completion_accepted=accepted; ++completions;
}
AXEBMS_RS485 my_bms;
static struct { BatteryMonitor_t battery; bool battery_seen_locked; unsigned lock; } g_app;
static uint32_t now;
static unsigned revoked, blade_stops;
static ModbusRxCallback callback;
uint32_t HAL_GetTick(void) { return now; }
void ModbusRtuBus_Init(ModbusRxCallback cb) { callback=cb; }
void ModbusRtuBus_Process(void) {}
bool ModbusRtuBus_ReadHoldingRegisters(uint8_t a,uint16_t s,uint16_t n,uint32_t t) {
 (void)a;(void)s;(void)n;(void)t;return true;
}
void RS485_SendData(uint8_t *p,uint16_t n) { (void)p;(void)n; }
uint16_t RS485_ReceiveData(uint8_t *p,uint16_t n) { (void)p;(void)n;return 0; }
static void RemoteBackend_RevokeAuthorization(void) { ++revoked; }
#define BLADE_SPEED_OFF 0.0f
static void BladeMotor_SetSpeed(float speed) { assert(speed==0.0f); ++blade_stops; }
'''
    motion = (CORE/'UserApp/app_Motion.c').read_text()
    hooks = function(motion, 'static void RefreshBatteryMonitor(') + '\n' + function(motion, 'static void ApplyBatteryAuthorization(')
    cases = r'''
static void sample(uint32_t tick, uint16_t voltage, uint16_t cell) {
 uint16_t words[16]={voltage,9880,250,250,250,250,250,250,280,250,3400,cell,0x0102,29,10000,2900};
 uint8_t pdu[32];
 for(unsigned i=0;i<16;i++) { pdu[i*2]=(uint8_t)(words[i]>>8);pdu[i*2+1]=(uint8_t)words[i]; }
 now=tick;callback(pdu,32);RefreshBatteryMonitor(now);ApplyBatteryAuthorization();
}
int main(void) {
 memset(&my_bms,0xA5,sizeof(my_bms));AXEBMS_RS485_Init(&my_bms,1);
 assert(!my_bms.sample_valid && !my_bms.last_good_tick && !my_bms.sample_sequence);
 AXEBMS_Data raw;uint32_t at;bool valid;
 my_bms.sample_sequence=1;
 assert(!AXEBMS_RS485_ReadTelemetry(&my_bms,&raw,&at,&valid)); /* preempted writer: bounded */
 my_bms.sample_sequence=0;
 BatteryMonitor_Init(&g_app.battery);g_app.lock=1;g_app.battery_seen_locked=true;
 SafetyOwnershipGate_Reset();
 RefreshBatteryMonitor(0);ApplyBatteryAuthorization();
 assert(g_app.lock && !g_app.battery.data_fresh && !revoked && !blade_stops);
 assert(!g_app.battery.stop_required && !g_app.battery.blade_inhibited);
 g_app.lock=0;ApplyBatteryAuthorization();assert(!g_app.lock);
 sample(0,5400,3350);assert(g_app.battery.data_fresh);
 assert(my_bms.data.CurCadc==-120 && my_bms.data.RSOC==29 && my_bms.data.FCC==10000);
 assert(g_app.battery.latest.rc_cah==2900 && g_app.battery.latest.min_temp_dc==250);
 g_app.lock=0;ApplyBatteryAuthorization();assert(!g_app.lock);
 for(unsigned t=500;t<=2500;t+=500) sample(t,4700,2930);
 assert(!g_app.lock && !revoked && blade_stops>0 && !g_app.battery.stop_required);
 assert(g_app.battery.low_latched && g_app.battery.blade_inhibited);
 assert(g_app.battery.latest.soc_percent==29);
 g_motion_blade_event_ready=true;g_motion_blade_event.kind=EXP3_PWM_CONFIRM;
 ApplyBatteryAuthorization();assert(completions==1 && !completion_accepted && !g_motion_blade_event_ready);
 g_motion_blade_event_ready=true;g_motion_blade_event.kind=EXP3_PWM_CANCEL;
 ApplyBatteryAuthorization();assert(completions==2 && completion_accepted && !g_motion_blade_event_ready);
 for(unsigned t=3000;t<=5000;t+=500) sample(t,5400,3350);
 g_app.lock=1;ApplyBatteryAuthorization();g_app.lock=0;ApplyBatteryAuthorization();
 assert(!g_app.lock && g_app.battery.low_latched && g_app.battery.blade_inhibited);
 now=8000;RefreshBatteryMonitor(now);ApplyBatteryAuthorization();
 assert(!g_app.lock && !g_app.battery.data_fresh && !revoked);
 assert(g_app.battery.low_latched && g_app.battery.blade_inhibited);
 sample(8500,5400,3350);
 assert(!g_app.lock && g_app.battery.data_fresh); /* No recovery/rearm barrier. */
 for(unsigned t=9000;t<=10500;t+=500) sample(t,5400,3350);
 g_app.lock=0;ApplyBatteryAuthorization();
 assert(!g_app.lock && !g_app.battery.stop_required && g_app.battery.low_latched);
 assert(g_app.battery.blade_inhibited); /* Diagnostic recovery cannot clear LOW. */
 AXEBMS_RS485_Init(&my_bms,1);BatteryMonitor_Init(&g_app.battery);
 g_app.lock=1;g_app.battery_seen_locked=true;
 sample(11000,5400,3350);
 assert(g_app.lock && !g_app.battery.low_latched); /* healthy reboot stays locked */
 g_app.lock=0;ApplyBatteryAuthorization();
 unsigned stops=blade_stops;
 now=14000;RefreshBatteryMonitor(now);ApplyBatteryAuthorization();
 assert(!g_app.lock && !revoked && blade_stops==stops && !g_app.battery.blade_inhibited);
 sample(14500,5400,0); /* Invalid raw cell data must remain diagnostic only. */
 assert(!g_app.battery.data_fresh && !g_app.lock && !revoked && blade_stops==stops);
 sample(15000,5400,3350);
 assert(g_app.battery.data_fresh && !g_app.lock && !g_app.battery.blade_inhibited);
 g_app.lock=1;sample(15500,5400,3350);assert(g_app.lock); /* Recovery never unlocks. */
 puts("PASS actual BMS decoder -> Motion -> gate: absent/stale/invalid feedback is diagnostic only; recovery never unlocks; LOW remains blade-only");
}
'''
    (work/'motion.c').write_text(prefix+hooks+cases)
    run([work/'motion.c', CORE/'UserDev/AXEBMS_RS485.c', monitor, gate],
        ['-Wno-unused-function', '-Wno-unused-parameter', '-DMOWER_BLADE_OK_ENABLED=1', '-DEXP3_ENABLED=1'])

    # Compile the actual final output function for all three supported drivers.
    chassis = (CORE/'UserDev/chassis_control.c').read_text()
    wheel_prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "safety_ownership_gate.h"
#include "battery_monitor.h"
#define DRIVER_TYPE_ODRIVE 0
#define DRIVER_TYPE_MSSD 1
#define DRIVER_TYPE_OID 2
#define OID_NODE_FL 2
#define OID_NODE_FR 1
#define OID_NODE_RL 4
#define OID_NODE_RR 3
#define MSSD_NODE_RIGHT 2
#define MSSD_NODE_LEFT 3
#define MSSD_CH_FRONT 0
#define MSSD_CH_REAR 1
static float output[4];
static unsigned count;
static void save(float motor_rpm) { assert(count<4);output[count++]=motor_rpm/GEAR_RATIO*PI*WHEEL_DIAMETER/60.0f; }
#if MOTOR_DRIVER_USED == DRIVER_TYPE_ODRIVE
static void Send_Motor_speed(unsigned id,float speed,float torque,unsigned reset) { (void)id;(void)torque;(void)reset;save(speed*60.0f); }
#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
static void MSSD_Set_Velocity(unsigned id,unsigned channel,int32_t rpm) { (void)id;(void)channel;save((float)rpm); }
static void osDelay(unsigned ms) { (void)ms; }
#else
static void OID_Set_Speed(unsigned id,int32_t erpm) { (void)id;save((float)erpm/OID_POLE_PAIRS); }
#endif
'''
    # Reuse physical conversion constants from the actual header.
    import re
    header = (CORE/'Inc/chassis_control.h').read_text()
    constants = ''.join(re.findall(r'^#define\s+(?:TRACK_WIDTH|WHEEL_DIAMETER|PI|GEAR_RATIO)\s+[^\n]+\n', header, re.M))
    constants += '#define OID_POLE_PAIRS 10\n'
    wheel_cases = r'''
int main(void) {
 for(unsigned owner=EXECUTION_OWNER_MANUAL_REMOTE;owner<=EXECUTION_OWNER_CRUISE;++owner) {
  SafetyOwnershipGateInput_t input={false,false,false,true,true,(ExecutionOwner_t)owner,1,0};
  SafetyOwnershipGate_Reset();SafetyOwnershipGate_Update(&input);
  BatteryMonitor_t battery;BatteryMonitor_Init(&battery);
  assert(!battery.data_fresh);
  SafetyOwnershipGate_SetBatteryPolicy(battery.stop_required,battery.blade_inhibited);
  const float intents[4][2]={{1.8f,0.0f},{0.0f,3.0f},{-1.2f,-3.0f},{0.1f,0.1f}};
  for(unsigned i=0;i<4;i++) {
   count=0;set_chassis_velocity((ExecutionOwner_t)owner,intents[i][0],0.0f,intents[i][1]);
   assert(count==4);
   float right=intents[i][0]-intents[i][1]*TRACK_WIDTH/2.0f;
   float left=intents[i][0]+intents[i][1]*TRACK_WIDTH/2.0f;
   for(unsigned j=0;j<4;j++)
    assert(fabsf(fabsf(output[j])-fabsf(j<2 ? right : left))<0.002f);
  }
  /* A real LOW monitor still allows identical full-speed travel. */
  BatterySample_t low={0};low.valid=true;low.pack_cv=4700;low.soc_percent=19;
  low.min_cell_mv=3000;low.max_cell_mv=3400;
  for(unsigned t=0;t<=2000;t+=500) {
   low.sample_ms=t;BatteryMonitor_Update(&battery,&low,t);
  }
  BatteryMonitor_Update(&battery,NULL,5000); /* Confirmed LOW plus lost feedback. */
  assert(battery.low_latched && battery.blade_inhibited && !battery.stop_required);
  SafetyOwnershipGate_SetBatteryPolicy(battery.stop_required,battery.blade_inhibited);
  count=0;set_chassis_velocity((ExecutionOwner_t)owner,1.8f,0.0f,0.0f);
  for(unsigned j=0;j<4;j++) assert(fabsf(fabsf(output[j])-1.8f)<0.002f);
  SafetyOwnershipGate_SetBatteryPolicy(true,true); /* Independent gate stop remains enforced. */
  count=0;set_chassis_velocity((ExecutionOwner_t)owner,1.8f,0.0f,3.0f);
  for(unsigned j=0;j<4;j++) assert(output[j]==0.0f);
 }
 printf("PASS real final wheel adapter %u: absent feedback and LOW retain RC/API/cruise full speed; explicit safety stop zeros output\n", MOTOR_DRIVER_USED);
}
'''
    (work/'wheels.c').write_text(constants+wheel_prefix+function(chassis, 'void set_chassis_velocity(')+wheel_cases)
    for driver in range(3):
        run([work/'wheels.c', gate, monitor], ['-DMOTOR_DRIVER_USED='+str(driver)])


print('PASS low-battery blade-only protection integration')
