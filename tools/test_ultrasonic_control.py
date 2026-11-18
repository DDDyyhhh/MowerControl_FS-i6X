#!/usr/bin/env python3
"""Exercise actual STICKS packing, i-BUS parser, Motion setting and speed limits."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from _harness_util import c_function

root = Path(__file__).resolve().parents[1]
radio = (root/'OpenI6X/FS-I6X/opentx/radio/src/targets/flysky/AFHDS2A_a7105.cpp').read_text()
backend = (root/'control_chassis/Core/UserDev/remote_backend.c').read_text()
motion = (root/'control_chassis/Core/UserApp/app_Motion.c').read_text()
config = (root/'control_chassis/Core/Inc/app_context.h').read_text()
start = radio.index('    case AFHDS2A_PACKET_STICKS:')
stop = radio.index('    case AFHDS2A_PACKET_FAILSAFE:', start)
sticks = radio[start:stop].split(':',1)[1].rsplit('break;',1)[0]
# RF failsafe retains the existing model settings; only STICKS carries CH13.
failsafe = radio[stop:radio.index('    case ',stop+8)]
assert 'g_mower_ultrasonic_request' not in failsafe
rf_source = r"""
#include <cassert>
#include <cstdint>
#include "ultrasonic_control.h"
#define AFHDS2A_CHANNELS_MAX 14
static uint8_t packet[37];
static int16_t channelOutputs[14];
static uint32_t g_exp3_uplink_channels;
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
static uint16_t g_mower_ultrasonic_request;
#endif
static void setChannelValue(uint8_t *p, unsigned ch, int16_t value) {
    uint16_t pulse=1500+value/2;
    p[9+2*ch]=pulse&255U; p[10+2*ch]=pulse>>8;
}
extern "C" void firmware_sticks(uint16_t setting,uint16_t *channels) {
    for(unsigned i=0;i<14;++i) channelOutputs[i]=i*20;
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    g_mower_ultrasonic_request=setting;
#endif
    g_exp3_uplink_channels=1100U|(1200UL<<16);
    STICKS
    for(unsigned i=0;i<14;++i) channels[i]=packet[9+2*i]|(uint16_t(packet[10+2*i])<<8);
    for(unsigned i=0;i<10;++i) assert(channels[i]==1500+i*10);
    assert(channels[10]==1100 && channels[11]==1200 && channels[13]==1630);
    assert(channels[12]==(MOWER_ULTRASONIC_AVOIDANCE_ENABLED ? setting : MOWER_ULTRASONIC_OFF_US));
}
""".replace('STICKS',sticks)
source = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote_backend.h"
#include "flysky_ibus_servo.h"
#include "exp3_board.h"
#include "telemetry_snapshot.h"
#include "ultrasonic_control.h"
RANGE_DEFINES
static Exp3BoardStatus board;
static RemoteCommand_t g_command;
static uint32_t g_now_ms;
static struct { uint8_t cfg_ultrasonic_en; float d1,d2,d3,d4,user_set_speed; } g_app;
void exp3_board_status(Exp3BoardStatus *out) { *out=board; }
void exp3_board_channels(uint32_t now,uint16_t ch4,uint16_t ch11,uint16_t ch12) {
    (void)now;(void)ch4;assert(ch11==1100 && ch12==1200);
}
extern void firmware_sticks(uint16_t setting,uint16_t *channels);
APPLY
static float distances[SR04_SENSOR_COUNT];
static bool SR04_ReadLastDistances(float out[SR04_SENSOR_COUNT]) {
    memcpy(out, distances, sizeof(distances));
    return true;
}
DISTANCES
LIMIT_DEFINES
static float actual_limit(void) {
LIMIT
    return target_speed;
}
static FlySkyIbusServo_t ibus;
static uint32_t now;
static void tick(uint16_t setting) {
    uint16_t channels[14]; uint8_t frame[32]={0x20,0x40};
    firmware_sticks(setting,channels);
    for(unsigned i=0;i<14;++i) { frame[2+i*2]=channels[i]&255;frame[3+i*2]=channels[i]>>8; }
    uint16_t checksum=0xffff;
    for(unsigned i=0;i<30;++i) checksum-=frame[i];
    frame[30]=checksum&255;frame[31]=checksum>>8;
    now+=10;
    FlySkyIbusServo_FeedReceived(&ibus,frame,32,now,now);
    FlySkyIbusSnapshot_t sample;
    assert(FlySkyIbusServo_ReadSnapshot(&ibus,&sample));
    memcpy(g_command.channels,sample.channels,sizeof(g_command.channels));
    g_now_ms=now;g_command.last_valid_ms=sample.last_valid_ms;
    g_command.connected=true;g_command.link_fresh=sample.link_fresh;
    g_command.stable_online=sample.stable_online;g_command.frame_sequence=sample.frame_sequence;
    RemoteBackend_ApplyUltrasonic();
}
int main(void) {
    FlySkyIbusConfig_t cfg=FlySkyIbusServo_DefaultConfig();
    FlySkyIbusServo_Init(&ibus,&cfg);
    board.bound=true;board.controller=1;board.hmi=2;
    for(unsigned i=0;i<50;++i) tick(MOWER_ULTRASONIC_OFF_US);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    assert(g_command.stable_online && g_app.cfg_ultrasonic_en==1);
    tick(MOWER_ULTRASONIC_ON_US); tick(MOWER_ULTRASONIC_OFF_US);
    assert(g_app.cfg_ultrasonic_en==0);
    tick(MOWER_ULTRASONIC_ON_US); assert(g_app.cfg_ultrasonic_en==1);
    // A repeatedly read snapshot is not a new command frame.
    g_command.channels[12]=2000; RemoteBackend_ApplyUltrasonic();
    assert(g_app.cfg_ultrasonic_en==1);
    ++g_command.frame_sequence;g_command.link_fresh=false;RemoteBackend_ApplyUltrasonic();
    assert(g_app.cfg_ultrasonic_en==1);
    g_command.link_fresh=true;g_command.stable_online=false;RemoteBackend_ApplyUltrasonic();
    assert(g_app.cfg_ultrasonic_en==1);
    g_command.stable_online=true;g_command.connected=false;RemoteBackend_ApplyUltrasonic();
    assert(g_app.cfg_ultrasonic_en==1);
    g_command.connected=true;++g_command.last_valid_ms;RemoteBackend_ApplyUltrasonic(); assert(g_app.cfg_ultrasonic_en==0);
    tick(1000);tick(1000); // Resume the parser sequence after synthetic snapshot checks.
    for(unsigned raw=1121;raw<1880;raw+=379) {
        tick(raw);assert(g_app.cfg_ultrasonic_en==1);
        tick(2000);assert(g_app.cfg_ultrasonic_en==1);
        tick(1000);tick(2000);assert(g_app.cfg_ultrasonic_en==0);
    }
    ++board.controller;tick(2000);assert(g_app.cfg_ultrasonic_en==1);
    // An old queued ON cannot establish a new session baseline.
    ++g_command.frame_sequence;g_command.channels[12]=1000;g_command.last_valid_ms=now-1;
    RemoteBackend_ApplyUltrasonic();
    tick(2000);tick(2000);assert(g_app.cfg_ultrasonic_en==1);
    tick(900);tick(1880);assert(g_app.cfg_ultrasonic_en==0);
    ++board.hmi;tick(2000);assert(g_app.cfg_ultrasonic_en==1);
    tick(1120);tick(2100);assert(g_app.cfg_ultrasonic_en==0);
    board.bound=false;RemoteBackend_ApplyUltrasonic();assert(g_app.cfg_ultrasonic_en==1);
    board.bound=true;tick(2000);assert(g_app.cfg_ultrasonic_en==1);
    tick(1000);g_command.frame_sequence=0xffffffffU;g_command.channels[12]=2000;++g_command.last_valid_ms;
    RemoteBackend_ApplyUltrasonic();assert(g_app.cfg_ultrasonic_en==0);
    g_command.frame_sequence=0;g_command.channels[12]=1000;++g_command.last_valid_ms;
    RemoteBackend_ApplyUltrasonic();assert(g_app.cfg_ultrasonic_en==1);
    // Arrival-time progress also survives the millisecond clock wrapping.
    ++board.controller;g_now_ms=0xffffffe0U;RemoteBackend_ApplyUltrasonic();
    ++g_command.frame_sequence;g_command.last_valid_ms=0xfffffff0U;g_command.channels[12]=1000;
    RemoteBackend_ApplyUltrasonic();assert(g_app.cfg_ultrasonic_en==1);
    ++g_command.frame_sequence;g_command.last_valid_ms=0x10U;g_command.channels[12]=2000;
    RemoteBackend_ApplyUltrasonic();assert(g_app.cfg_ultrasonic_en==0);
#else
    assert(g_command.stable_online && g_app.cfg_ultrasonic_en==0);
    tick(MOWER_ULTRASONIC_ON_US);
    assert(g_command.channels[12]==MOWER_ULTRASONIC_OFF_US);

    // Inject CH13 from an older transmitter and exercise both a fresh
    // stable session and a session replacement with the same input.
    const uint16_t pulses[]={900,1000,1120,1500,1880,2000,2100};
    for (unsigned i=0;i<sizeof(pulses)/sizeof(pulses[0]);++i) {
        ++g_command.frame_sequence;
        g_command.last_valid_ms=++now;
        g_now_ms=now;
        g_command.channels[12]=pulses[i];
        RemoteBackend_ApplyUltrasonic();
        assert(g_app.cfg_ultrasonic_en==0);
        ++board.controller;
        ++board.hmi;
        RemoteBackend_ApplyUltrasonic();
        assert(g_app.cfg_ultrasonic_en==0);
    }
    board.bound=false;
    RemoteBackend_ApplyUltrasonic();
    assert(g_app.cfg_ultrasonic_en==0);
#endif
    // Existing distance substitution and all three speed thresholds stay active.
    g_app.user_set_speed=1.1f;
    const float ranges[]={49,50,51,100,101,150,151};
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    const float limits[]={MOWER_UC_1_MAX_SPEED,MOWER_UC_1_MAX_SPEED,
        MOWER_UC_2_MAX_SPEED,MOWER_UC_2_MAX_SPEED,MOWER_UC_3_MAX_SPEED,MOWER_UC_3_MAX_SPEED,1.1f};
#endif
    for(unsigned probe=0;probe<4;++probe) for(unsigned range=0;range<7;++range) {
        for(unsigned i=0;i<4;++i) distances[i]=250;
        distances[probe]=ranges[range];
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
        g_app.cfg_ultrasonic_en=1;RefreshUltrasonicDistances(2048,2048);
        assert(actual_limit()==limits[range]);
#endif
        g_app.cfg_ultrasonic_en=0;RefreshUltrasonicDistances(2048,2048);
        assert(g_app.d1==250 && g_app.d2==250 && g_app.d3==250 && g_app.d4==250);
        assert(actual_limit()==1.1f);
    }
    TelemetrySources_t sources={0};TelemetrySnapshot_t on,off;
    TelemetrySnapshot_Reset();sources.ultrasonic_enabled=true;
    TelemetrySnapshot_Update(&sources,1000);assert(TelemetrySnapshot_Read(&on));
    sources.ultrasonic_enabled=false;
    TelemetrySnapshot_Update(&sources,1000);assert(TelemetrySnapshot_Read(&off));
    unsigned enabled=on.values[TELEMETRY_VALUE_DETAILED_FAULT];
    unsigned disabled=off.values[TELEMETRY_VALUE_DETAILED_FAULT];
    assert((enabled^disabled)==MOWER_ULTRASONIC_ENABLED);
    assert((enabled&MOWER_STATUS_ALARM_MASK)==(disabled&MOWER_STATUS_ALARM_MASK));
    assert(on.values[TELEMETRY_VALUE_FAST_STATUS]==off.values[TELEMETRY_VALUE_FAST_STATUS]);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    puts("PASS optional CH13 RF/i-BUS/Motion, session/invalid/stale/frame wrap, four probes and speed thresholds");
#else
    puts("PASS disabled CH13 RF always off, older requests and reconnect cannot enable avoidance, no distance limit");
#endif
}
"""
distance_code=c_function(motion,'static void RefreshUltrasonicDistances(')
sr04_header=(root/'control_chassis/Core/Inc/sr04_uart.h').read_text()
chassis_header=(root/'control_chassis/Core/Inc/chassis_control.h').read_text()
range_defines='\n'.join(re.findall(r'^#define (?:SR04_SENSOR_COUNT|SR04_NO_DISTANCE_CM|RC_DEADZONE) .+$',
                                  sr04_header+'\n'+chassis_header,re.M))
a=motion.index('        float target_speed = g_app.user_set_speed;')
b=motion.index('        if (g_app.current_ramped_speed < target_speed)',a)
source=source.replace('APPLY',c_function(backend,'static void RemoteBackend_ApplyUltrasonic(void)'))
source=source.replace('RANGE_DEFINES',range_defines).replace('DISTANCES',distance_code).replace('LIMIT_DEFINES','\n'.join(re.findall(r'^#define MOWER_UC_[123]_MAX_SPEED.*$',config,re.M))).replace('LIMIT',motion[a:b])
with tempfile.TemporaryDirectory(prefix='ultrasonic-control-') as work:
    work=Path(work)
    (work/'radio.cpp').write_text(rf_source);(work/'test.c').write_text(source)
    defs=['-DEXP3_ENABLED=1','-DMOWER_BLADE_OK_ENABLED=1','-DREMOTE_BACKEND_A28_E34=0','-DREMOTE_BACKEND_FLYSKY_IBUS=1']
    for avoidance in (0, 1):
        flags=['-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*defs,f'-DMOWER_ULTRASONIC_AVOIDANCE_ENABLED={avoidance}','-I'+str(root/'control_chassis/Core/Inc'),'-I'+str(root/'experiments/ticket03')]
        subprocess.run(['c++','-std=c++11',*flags,'-c',str(work/'radio.cpp'),'-o',str(work/'radio.o')],check=True)
        cc=os.environ.get('CC','cc')
        objects=[]
        for i,path in enumerate([work/'test.c',root/'control_chassis/Core/UserDev/flysky_ibus_servo.c',root/'control_chassis/Core/UserApp/telemetry_snapshot.c']):
            obj=work/f'{i}.o';objects.append(str(obj))
            subprocess.run([cc,'-std=c99',*flags,'-c',str(path),'-o',str(obj)],check=True)
        subprocess.run(['c++','-fsanitize=address,undefined',*objects,str(work/'radio.o'),'-o',str(work/'test')],check=True)
        subprocess.run([str(work/'test')],check=True)
