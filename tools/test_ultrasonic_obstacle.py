#!/usr/bin/env python3
# Run real UART distance parsing -> Motion warning -> telemetry -> E0 response.
import os
from pathlib import Path
import subprocess
import tempfile
import re
from _harness_util import c_function

root=Path(__file__).resolve().parents[1]
core=root/'control_chassis/Core'
motion=(core/'UserApp/app_Motion.c').read_text()
a=motion.index('  float ultrasonic_distances[SR04_SENSOR_COUNT];')
b=motion.index('\n#endif',a)
source=r'''
#define MOWER_BLADE_OK_ENABLED 1
#define REMOTE_BACKEND_A28_E34 0
#define REMOTE_BACKEND_FLYSKY_IBUS 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sr04_uart.h"
#include "cmsis_os.h"
#include "sens_ibus_responder.h"
#include "mower_fast_status.h"
#include "ultrasonic_control.h"
#include "remote_backend_config.h"
#include "motor_driver_config.h"
#include "safety_ownership_gate.h"
#include <math.h>
CONSTANTS
UART_HandleTypeDef huart6={USART6};
osMessageQueueId_t Queue_SR04DataHandle;
static uint32_t now;
static unsigned critical_depth, dma_starts;
static bool inject_frame;
static struct {
    unsigned cfg_ultrasonic_en,cfg_lawn_protect_en;
    float d1,d2,d3,d4,speed,turn_speed,user_set_speed,current_ramped_speed;
} g_app={.d1=250,.d2=250,.d3=250,.d4=250};
static uint16_t simulated_throttle=2048, simulated_steering=2048;
void test_enter(void) { assert(critical_depth==0); ++critical_depth; }
void test_exit(void) {
    assert(critical_depth==1); --critical_depth;
    if (inject_frame) {
        inject_frame=false;
        uint8_t frame[]={0xcb,0x55,4,180,50,170,160};
        SR04_Process_Data(frame,sizeof(frame));
    }
}
uint32_t HAL_GetTick(void) { return now; }
int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u,uint8_t *p,uint16_t n) {
    assert(u==&huart6 && p && n==64);++dma_starts;return 0;
}
int osMessageQueuePut(osMessageQueueId_t queue,const void *msg,uint8_t priority,uint32_t timeout) {
    (void)queue;(void)msg;(void)priority;(void)timeout;return 0;
}
static void sample(unsigned d1,unsigned d2,unsigned d3,unsigned d4) {
    uint8_t frame[]={0xcb,0x55,4,(uint8_t)d1,(uint8_t)d2,(uint8_t)d3,(uint8_t)d4};
    SR04_Process_Data(frame,sizeof(frame));
}
DISTANCE_UPDATE
static void motion_distance_update(void) {
    RefreshUltrasonicDistances(simulated_throttle, simulated_steering);
}
static void motion_speed_step(void) {
    motion_distance_update();
WALK_SPEED_RAMP
}
static void settle_walk_speed(void) {
    for (unsigned i=0;i<20;++i) motion_speed_step();
}
static uint16_t publish(void) {
    TelemetrySources_t sources={0};
MOTION
    TelemetrySnapshot_Update(&sources,now);
    TelemetrySnapshot_t snapshot;
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(!(snapshot.values[TELEMETRY_VALUE_FAST_STATUS] & (MOWER_FAST_FAULT|MOWER_FAST_EMERGENCY)));
    return snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT];
}
static bool read_snapshot(void *ctx,TelemetrySnapshot_t *out) {(void)ctx;return TelemetrySnapshot_Read(out);}
static uint8_t response[8], response_length;
static bool send_response(void *ctx,const uint8_t *p,uint8_t n) {
    (void)ctx;memcpy(response,p,n);response_length=n;return true;
}
static void check_wire(uint16_t expected) {
    SensIbusResponder_t responder;
    SensIbusResponder_Init(&responder,read_snapshot,0,send_response,0);
    uint8_t request[]={4,0xa8,0x53,0xff};
    SensIbusResponder_Feed(&responder,request,sizeof(request));
    assert(response_length==6 && response[0]==6 && response[1]==0xa8);
    assert((uint16_t)(response[2]|((uint16_t)response[3]<<8))==expected);
    uint16_t checksum=0xffffU;
    for(unsigned i=0;i<4;++i) checksum-=response[i];
    assert((uint16_t)(response[4]|((uint16_t)response[5]<<8))==checksum);
}

static int32_t erpm[5];
static unsigned wheel_calls;
static void OID_Set_Speed(unsigned id,int32_t speed) {assert(id>=1 && id<=4);erpm[id]=speed;++wheel_calls;}
VELOCITY
REVERSE
RC_CONTROL
static void gate(ExecutionOwner_t owner) {
    SafetyOwnershipGate_Reset();
    SafetyOwnershipGateInput_t input={false,false,false,true,true,owner,1,0};
    SafetyOwnershipGate_Update(&input);
}
static float wheel(unsigned id) {
    float sign=(id==OID_NODE_FR || id==OID_NODE_RR)?-1.0f:1.0f;
    return sign*erpm[id]/OID_POLE_PAIRS/GEAR_RATIO*PI*WHEEL_DIAMETER/60.0f;
}
static void rc(unsigned ly,unsigned rx) {
    simulated_throttle=ly;simulated_steering=rx;
    motion_distance_update();
    wheel_calls=0;set_chassis_rc_control(ly,rx);assert(wheel_calls==4);
}
static void stopped(void) {for(unsigned i=1;i<=4;++i) assert(erpm[i]==0);}
static void zero_side_travel_case(void) {
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.turn_speed=1.0f;g_app.user_set_speed=1.1f;g_app.cfg_ultrasonic_en=1;
    SR04_UART_Init();now=3000;sample(0,200,200,200);
    settle_walk_speed();
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    rc(4095,2048);const float forward=wheel(1);
    rc(0,2048);const float reverse=wheel(1);
    rc(2048,0);const float turn=wheel(1)*wheel(2);
    ++now;sample(0,39,200,200);sample(0,39,200,200);
    settle_walk_speed();
    assert(publish()&MOWER_ULTRASONIC_NEAR);
    rc(0,2048);const float escape=wheel(1);
    g_app.cfg_ultrasonic_en=0;settle_walk_speed();
    rc(4095,2048);const float without_avoidance=wheel(1);
    assert(fabsf(escape+0.2f)<0.002f && fabsf(without_avoidance-1.1f)<0.002f);
    if (fabsf(forward-1.1f)>=0.002f || fabsf(reverse+0.88f)>=0.002f)
        fprintf(stderr,"clear front + side raw 0: forward %.3f, reverse %.3f, escape %.3f, avoidance off %.3f m/s\n",
                forward,reverse,escape,without_avoidance);
    assert(fabsf(forward-1.1f)<0.002f && fabsf(reverse+0.88f)<0.002f && turn<0.0f);
    puts("PASS side zero + clear front: normal forward/reverse/pivot with avoidance on; front obstacle still stops or escapes");
}
static void missing_ranges_cases(void) {
    float distances[4];
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.cfg_ultrasonic_en=1;g_app.user_set_speed=1.1f;
    SR04_UART_Init();now=4000;sample(0,0,0,0);
    assert(!SR04_ReadDistances(distances) && !(publish()&MOWER_ULTRASONIC_NEAR));
    settle_walk_speed();rc(4095,2048);assert(fabsf(wheel(1)-1.1f)<0.002f);

    // Side-only near restricts straight travel even after a blank reading.
    sample(38,180,180,180);settle_walk_speed();
    rc(4095,0);assert(fabsf(wheel(1)-0.2f)<0.002f && fabsf(wheel(2)-0.2f)<0.002f);
    rc(0,4095);assert(fabsf(wheel(1)+0.2f)<0.002f && fabsf(wheel(2)+0.2f)<0.002f);
    rc(2048,0);stopped();
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    sample(0,180,180,180);settle_walk_speed();rc(4095,2048);
    assert(fabsf(wheel(1)-0.2f)<0.002f);
    sample(180,180,180,180);sample(180,180,180,180);settle_walk_speed();
    rc(4095,2048);assert(fabsf(wheel(1)-1.1f)<0.002f);

    // Positive short-range readings retain the original blind-zone clamp.
    sample(180,1,180,180);sample(180,1,180,180);
    assert(SR04_ReadDistances(distances) && distances[1]==30 && (publish()&MOWER_ULTRASONIC_NEAR));
    settle_walk_speed();rc(4095,2048);stopped();rc(0,2048);assert(fabsf(wheel(1)+0.2f)<0.002f);

    // Fresh side data does not make an absent front reading fresh. Hold the
    // forward stop and allow straight reverse until a valid clear reading.
    sample(180,0,180,180);
    assert(SR04_ReadDistances(distances) && distances[1]==250 && SR04_GetDistance(1)==30);
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    settle_walk_speed();rc(4095,2048);stopped();rc(0,2048);
    assert(fabsf(wheel(1)+0.2f)<0.002f);
    sample(0,0,0,0);
    assert(SR04_ReadLastDistances(distances) && distances[1]==30);
    assert(!SR04_ReadDistances(distances));
    rc(2048,2048);stopped();rc(4095,2048);stopped();
    sample(180,180,180,180);sample(180,180,180,180);settle_walk_speed();
    rc(4095,2048);assert(fabsf(wheel(1)-1.1f)<0.002f);
    puts("PASS missing range handling: side-only cap, front stop with reverse, no stale popup, fresh clear restores normal travel");
}
static void no_popup_recovery_cases(void) {
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.cfg_ultrasonic_en=1;g_app.user_set_speed=1.1f;g_app.turn_speed=1;
    unsigned failures=0;
    // Only the side is near. The home must have no front popup.
    SR04_UART_Init();now=4750;sample(38,180,180,180);
    settle_walk_speed();assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    rc(4095,2048);const float side_forward=wheel(1);
    rc(0,2048);const float side_reverse=wheel(1);
    if (fabsf(side_forward-0.2f)>=0.002f || fabsf(side_reverse+0.2f)>=0.002f) {
        fprintf(stderr,"fresh side 38 cm, front clear, no popup: forward %.3f, reverse %.3f m/s\n",
                side_forward,side_reverse);
        ++failures;
    }
    // A previously accepted front obstacle has aged out of the popup.
    SR04_UART_Init();sample(180,39,180,180);settle_walk_speed();
    assert(publish()&MOWER_ULTRASONIC_NEAR);
    now+=SR04_SAMPLE_FRESH_MS;
    settle_walk_speed();assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    rc(4095,2048);stopped();
    rc(0,2048);const float stale_reverse=wheel(1);
    if (fabsf(stale_reverse+0.2f)>=0.002f) {
        fprintf(stderr,"last front 39 cm, UART sample expired, no popup: reverse %.3f m/s\n",stale_reverse);
        ++failures;
    }
    // At 45 cm, the fresh front stop already allows reverse without a popup.
    SR04_UART_Init();sample(180,45,180,180);settle_walk_speed();
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    rc(4095,2048);stopped();rc(0,2048);assert(fabsf(wheel(1)+0.2f)<0.002f);
    assert(failures==0);
    puts("PASS no-popup cases: side-only straight travel and reverse from last known front stop");
}
static void unplugged_module_cases(void) {
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.cfg_ultrasonic_en=1;g_app.user_set_speed=1.1f;g_app.turn_speed=1;
    // Unplug after an accepted front obstacle. Stay stopped while the
    // operator is still requesting forward/turning, even after timeout.
    SR04_UART_Init();now=6000;sample(180,39,180,180);settle_walk_speed();
    rc(4095,2048);stopped();assert(publish()&MOWER_ULTRASONIC_NEAR);
    now+=SR04_SAMPLE_FRESH_MS;
    rc(4095,2048);stopped();assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    rc(2048,0);stopped();
    rc(0,2048);assert(fabsf(wheel(1)+0.2f)<0.002f);

    // Releasing both sticks explicitly is enough to recover after unplug.
    rc(2048,2048);stopped();settle_walk_speed();
    rc(4095,2048);
    if (fabsf(wheel(1)-1.1f)>=0.002f)
        fprintf(stderr,"module unplugged, 1 second elapsed + both sticks centered: forward %.3f m/s\n",wheel(1));
    assert(fabsf(wheel(1)-1.1f)<0.002f);
    rc(0,2048);assert(fabsf(wheel(1)+0.88f)<0.002f);
    rc(2048,0);assert(wheel(1)*wheel(2)<0);

    // On module reconnection, new genuine near data stops forward again.
    sample(180,39,180,180);
    rc(4095,2048);stopped();assert(publish()&MOWER_ULTRASONIC_NEAR);
    puts("PASS ultrasonic unplug: held front stop until neutral, normal manual after timeout, stops again on reconnect");
}
static void escape_cases(void) {
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.cfg_ultrasonic_en=1;g_app.user_set_speed=1.1f;g_app.speed=1.8f;g_app.turn_speed=1;
    SR04_UART_Init();now=5000;sample(200,39,200,200);
    // Ignore the old high ramp: no forward/pivot; proportional straight reverse.
    rc(4095,0);stopped();rc(2048,4095);stopped();
    for(unsigned steering=0;steering<=4095;steering+=819) {
        rc(0,steering);
        for(unsigned id=1;id<=4;++id) assert(wheel(id)<0 && fabsf(wheel(id)+0.2f)<0.002f);
        assert(fabsf(wheel(1)-wheel(2))<0.002f);
    }
    rc(1024,0);assert(wheel(1)<0 && wheel(1)>-0.2f);
    g_app.speed=0;rc(0,4095);assert(fabsf(wheel(1)+0.2f)<0.002f);
    g_app.user_set_speed=0;rc(0,2048);stopped();
    g_app.user_set_speed=0.1f;rc(0,2048);assert(fabsf(wheel(1)+0.08f)<0.002f);
    g_app.user_set_speed=1.1f;
    for(unsigned cm=40;cm<=51;++cm) {
        SR04_UART_Init();++now;sample(39,cm,200,39);
        rc(0,0);assert(fabsf(wheel(1)+0.2f)<0.002f);
        rc(4095,0);
        if(cm<=50) stopped();
        else assert(fabsf(wheel(1)-0.2f)<0.002f); // Only the side cap applies.
    }
    SR04_UART_Init();sample(200,39,200,200);motion_distance_update();now+=1000;
    rc(4095,2048);stopped();
    rc(0,2048);assert(fabsf(wheel(1)+0.2f)<0.002f);
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    SR04_UART_Init();sample(200,39,200,200);
    for(unsigned reason=0;reason<7;++reason) {
        gate(EXECUTION_OWNER_MANUAL_REMOTE);
        SafetyOwnershipGateInput_t input={false,false,false,true,true,EXECUTION_OWNER_MANUAL_REMOTE,2,0};
        if(reason==0) input.physical_latched=true;
        if(reason==1) input.software_locked=true;
        if(reason==2) input.recoverable_remote_latched=true;
        if(reason==3) input.link_stable=false;
        if(reason==4) input.run_permitted=false;
        if(reason==5) input.selected_owner=EXECUTION_OWNER_API;
        SafetyOwnershipGate_Update(&input);
        if(reason==6) SafetyOwnershipGate_SetBatteryPolicy(true,true);
        rc(0,0);stopped();
    }
    gate(EXECUTION_OWNER_MANUAL_REMOTE);SafetyOwnershipGate_SetBatteryPolicy(false,true);
    rc(0,2048);assert(fabsf(wheel(1)+0.2f)<0.002f); // Blade-only LOW still permits travel.
    gate(EXECUTION_OWNER_API);wheel_calls=0;
    set_chassis_velocity(EXECUTION_OWNER_API,0.6f,0,0);assert(wheel_calls==4);
    assert(fabsf(wheel(1)-0.6f)<0.002f); // No API policy change.
    gate(EXECUTION_OWNER_MANUAL_REMOTE);
    g_app.cfg_ultrasonic_en=0;g_app.speed=1.1f;rc(0,2048);
    assert(fabsf(wheel(1)+0.88f)<0.002f); // Off retains ordinary reverse factor.
    g_app.cfg_ultrasonic_en=1;SR04_UART_Init();sample(200,51,51,200);g_app.speed=0.3f;
    rc(4095,2048);assert(fabsf(wheel(1)-0.3f)<0.002f);
    puts("PASS actual RC/final OID adapter: front<=50cm reverse <=0.2m/s, proportional/straight, zero knob, ramp-independent stop, stale front escape, side-only straight travel, all safety gates, LOW and API isolation");
}
int main(void) {
    float distances[4];
    TelemetrySnapshot_Reset();g_app.cfg_ultrasonic_en=1;
    SR04_UART_Init();assert(dma_starts==1 && !SR04_ReadDistances(distances));
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    // Strictly below 40: 39 warns, 40 and 41 clear. Only d2/d3 are forward.
    for(unsigned sensor=0;sensor<4;++sensor) for(unsigned cm=39;cm<=41;++cm) {
        SR04_UART_Init();now+=10;
        unsigned values[]={200,200,200,200};values[sensor]=cm;
        sample(values[0],values[1],values[2],values[3]);
        assert(SR04_ReadDistances(distances) && distances[sensor]==cm);
        const bool near=(sensor==1 || sensor==2) && cm<40;
        uint16_t word=publish();assert(!!(word&MOWER_ULTRASONIC_NEAR)==near);
        assert(word&MOWER_ULTRASONIC_ENABLED);assert(!(word&MOWER_STATUS_ALARM_MASK));
        check_wire(word);
    }
    SR04_UART_Init();now=1000;sample(200,39,200,200);
    assert(publish()&MOWER_ULTRASONIC_NEAR);
    now=1999;assert(SR04_ReadDistances(distances) && (publish()&MOWER_ULTRASONIC_NEAR));
    uint8_t bad[]={0,0,4,39,39,39,39};SR04_Process_Data(bad,sizeof(bad));
    now=2000;assert(!SR04_ReadDistances(distances) && !(publish()&MOWER_ULTRASONIC_NEAR));
    now=2010;sample(200,39,200,200);
    g_app.cfg_ultrasonic_en=0;assert(!(publish()&(MOWER_ULTRASONIC_NEAR|MOWER_ULTRASONIC_ENABLED)));
    g_app.cfg_ultrasonic_en=1;assert(publish()&MOWER_ULTRASONIC_NEAR);
    sample(200,40,200,200);assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    // Existing glitch filter still needs two consistent large jumps.
    SR04_UART_Init();sample(200,200,200,200);
    sample(200,39,200,200);assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    sample(200,39,200,200);assert(publish()&MOWER_ULTRASONIC_NEAR);
    // A reader returns one coherent copied frame even if Sensor publishes next.
    inject_frame=true;assert(SR04_ReadDistances(distances));
    assert(distances[0]==200 && distances[1]==39 && distances[2]==200 && distances[3]==200);
    assert(critical_depth==0);
    assert(SR04_ReadDistances(distances));
    assert(distances[0]==180 && distances[1]==50 && distances[2]==170 && distances[3]==160);
    assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    // Time wraps normally and incomplete packets cannot refresh sample age.
    SR04_UART_Init();now=0xffffffe0U;sample(200,200,39,200);
    now+=999;assert(publish()&MOWER_ULTRASONIC_NEAR);
    uint8_t partial[]={0xcb,0x55,4,39};SR04_Process_Data(partial,sizeof(partial));
    ++now;assert(!(publish()&MOWER_ULTRASONIC_NEAR));
    SR04_UART_Init();assert(!SR04_ReadDistances(distances) && !(publish()&MOWER_ULTRASONIC_NEAR));
    zero_side_travel_case();
    no_popup_recovery_cases();
    missing_ranges_cases();
    unplugged_module_cases();
    escape_cases();
    puts("PASS production SR04/Motion/E0: front d2/d3 only, 39/40/41cm, off/no-data/1000ms stale, filter, atomic snapshot and clock wrap; no fault/emergency");
}
'''.replace('MOTION',motion[a:b])
chassis=(core/'UserDev/chassis_control.c').read_text()
backend=(core/'UserDev/remote_backend.c').read_text()
header=(core/'Inc/chassis_control.h').read_text()
constants=''.join(re.findall(r'^\s*#define\s+(?:RC_DEADZONE|TRACK_WIDTH|WHEEL_DIAMETER|PI|GEAR_RATIO|OID_NODE_FL|OID_NODE_FR|OID_NODE_RL|OID_NODE_RR|OID_POLE_PAIRS)\s+[^\n]+\n',header,re.M))
source=source.replace('CONSTANTS',constants).replace('VELOCITY',c_function(chassis,'void set_chassis_velocity(')).replace('REVERSE',c_function(backend,'float RemoteBackend_ApplyReverseSpeed(')).replace('RC_CONTROL',c_function(chassis,'void set_chassis_rc_control('))
ramp_start=motion.index('        float target_speed = g_app.user_set_speed;')
ramp_end=motion.index('        g_app.speed = g_app.current_ramped_speed;',ramp_start)
ramp_end=motion.index('\n',ramp_end)
app_config=(core/'Inc/app_context.h').read_text()
speed_defines='\n'.join(re.findall(r'^#define (?:MOWER_UC_[123]_MAX_SPEED|SPEED_RAMP_(?:UP|DOWN)_STEP).+$',app_config,re.M))
source=source.replace('WALK_SPEED_RAMP',motion[ramp_start:ramp_end]).replace('DISTANCE_UPDATE',c_function(motion,'static void RefreshUltrasonicDistances('))
source=source.replace('UART_HandleTypeDef huart6=',speed_defines+'\nUART_HandleTypeDef huart6=',1)
with tempfile.TemporaryDirectory(prefix='ultrasonic-obstacle-') as directory:
    work=Path(directory)
    (work/'main.h').write_text('''#ifndef __MAIN_H
#define __MAIN_H
#include <stdint.h>
typedef struct { void *Instance; } UART_HandleTypeDef;
#define USART6 ((void *)6)
uint32_t HAL_GetTick(void);
int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
#endif
''')
    (work/'usart.h').write_text('#include "main.h"\nextern UART_HandleTypeDef huart6;\n')
    (work/'cmsis_os.h').write_text('''#ifndef TEST_CMSIS_OS
#define TEST_CMSIS_OS
#include <stdint.h>
typedef void *osMessageQueueId_t;
typedef struct {uint8_t data[64];uint16_t len;} UartRxMsg_t;
int osMessageQueuePut(osMessageQueueId_t,const void *,uint8_t,uint32_t);
#endif
''')
    (work/'FreeRTOS.h').write_text('')
    (work/'task.h').write_text('''void test_enter(void);void test_exit(void);
#define taskENTER_CRITICAL() test_enter()
#define taskEXIT_CRITICAL() test_exit()
''')
    (work/'test.c').write_text(source)
    flags=['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',
           '-I'+str(work),'-I'+str(core/'Inc'),'-I'+str(root/'experiments/ticket03'),'-include',str(work/'main.h')]
    sources=[work/'test.c',core/'UserDev/sr04_uart.c',core/'UserApp/telemetry_snapshot.c',core/'UserDev/sens_ibus_responder.c',core/'UserApp/safety_ownership_gate.c']
    subprocess.run([os.environ.get('CC','cc'),*flags,*map(str,sources),'-lm','-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
