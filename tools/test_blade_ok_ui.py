#!/usr/bin/env python3
"""Execute the actual 128x64 home and overlay renderers against display fakes."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = root / 'OpenI6X/FS-I6X/opentx/radio/src/gui/128x64/view_main.cpp'
text = source.read_text()
def function(signature, source_text=text):
    start = source_text.index(signature)
    opened = source_text.index('{', start)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source_text[end] == '{') - (source_text[end] == '}')
        end += 1
    return source_text[start:end]
header = root / 'OpenI6X/FS-I6X/opentx/radio/src/gui/128x64/mower_ui.h'
probe = root / 'experiments/ticket03/exp3_probe.h'
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include "MOWER_HEADER"
#include "PROBE_HEADER"
#include "BOARD_HEADER"
using coord_t=int;
using event_t=int;
using LcdFlags=uint32_t;
enum { INVERS=0x02, FORCE=0x02, ERASE=0x04, SMLSIZE=0x0200, MIDSIZE=0x0300,
       LEADING0=0x10, PREC1=0x20, PREC2=0x30 };
#define MODE(flags) ((((int8_t)(flags) & 0x30) - 0x10) >> 4)
enum { VIEW_MOWER=0, KEY_ENTER=0, KEY_MENU=0, KEY_EXIT=1, KEY_RIGHT=4, KEY_LEFT=5, KEY_UP=3, KEY_DOWN=2 };
#define EVT_KEY_BREAK(x) (0x20|(x))
#define EVT_KEY_LONG(x) (0x80|(x))
#define EVT_KEY_REPT(x) (0x40|(x))
static std::vector<std::string> labels;
struct TextPosition { int x, y; std::string value; };
static std::vector<TextPosition> textPositions;
static uint8_t pixels[64][128];
static void lcdDrawPoint(int x,int y,int flags) {
    assert(x>=0 && x<128 && y>=0 && y<64);
    pixels[y][x]=(flags & ERASE) ? 0 : 1;
}
static void lcdDrawSolidFilledRect(int x,int y,int w,int h,int flags) {
    assert(x>=0 && y>=0 && x+w<=128 && y+h<=64);
    for(int dy=0;dy<h;++dy) for(int dx=0;dx<w;++dx) lcdDrawPoint(x+dx,y+dy,flags);
}
static void lcdDrawRect(int x,int y,int w,int h) {
    for(int dy=0;dy<h;++dy) for(int dx=0;dx<w;++dx)
        if(dx==0 || dy==0 || dx==w-1 || dy==h-1) lcdDrawPoint(x+dx,y+dy,FORCE);
}
static coord_t mowerZhText(coord_t,coord_t,const char*,bool inverse=false);
static coord_t mowerZhDraw(coord_t,coord_t,const char*,bool);
static coord_t lcdNextPos;
static void lcdDrawChar(coord_t x,coord_t y,unsigned char c,LcdFlags flags) {
    assert(c>=32 && c<128);
    if(c!=' ') lcdDrawPoint(x,y+2,(flags & INVERS) ? ERASE : FORCE);
}
static void lcdDrawText(coord_t x,coord_t y,const char *value,LcdFlags flags) {
    labels.emplace_back(value);
    textPositions.push_back({x,y,value});
    lcdNextPos=x;
    // Model glyph advances for the numeric and unit strings used here.
    const bool large=flags==MIDSIZE;
    assert(y>=0 && y+(large ? 12 : 6)<=64);
    for(const char *c=value;*c;++c) {
      lcdNextPos += large ? (*c=='.' ? 3 : *c=='-' ? 6 : 7) : (*c=='m' ? 6 : 5);
      lcdDrawPoint(lcdNextPos-2,y+3,FORCE);
    }
    assert(lcdNextPos<=128);
}
void lcdDrawNumber(coord_t x,coord_t y,int val,LcdFlags flags,uint8_t len=0);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
volatile uint16_t g_mower_ultrasonic_request=MOWER_ULTRASONIC_ON_US;
#endif
static unsigned ticks=100;
static unsigned get_tmr10ms() { return ticks; }
static int channelOutputs[14];
static int channelCenters[14]={1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500};
#define PPM_CH_CENTER(x) channelCenters[x]
static struct { unsigned view; int vBatMin= -20; int vBatMax= -36; } g_eeGeneral;
static unsigned g_vbat100mV=78;
static bool streaming;
static unsigned rssi=100;
#define TELEMETRY_STREAMING() streaming
#define TELEMETRY_RSSI() rssi
#define TELEM_TYPE_CUSTOM 0
#define MAX_TELEMETRY_SENSORS 12
struct TelemetrySensor { int type; uint16_t id; };
struct TelemetryItem {
    bool available=false, old=false; int32_t value=0;
    bool isAvailable() const { return available; }
    bool isOld() const { return old; }
};
static struct { TelemetrySensor telemetrySensors[MAX_TELEMETRY_SENSORS]; } g_model;
static TelemetryItem telemetryItems[MAX_TELEMETRY_SENSORS];
static MowerUiSnapshot fixture;
static bool renderFixture;
static void mowerUiRead(MowerUiSnapshot &data) { if(renderFixture) data=fixture; else mowerUiReadLive(data); }
template <typename T> T limit(T lo,T value,T hi) { return value<lo ? lo : value>hi ? hi : value; }
static unsigned killedKey=99;
static void killEvents(int event) { killedKey=event & 0x1F; }
static unsigned pushedMenu;
static int menuTabGeneral[1]={11};
static int menuModelSelect=12;
static void pushMenu(int menu) { pushedMenu=menu; }
static Exp3BoardStatus board;
static unsigned submissions, lastSubmitKind;
void exp3_board_status(Exp3BoardStatus *s) { *s=board; }
void exp3_board_restart_session(uint32_t) { board.bound=false;board.cancel_failed=true; }
bool exp3_board_submit(const Exp3Message *message) { ++submissions; lastSubmitKind=message->kind; return true; }
static bool known;
static uint16_t runtime, authority;
static bool exp3_board_authority(uint32_t,uint16_t *word) { *word=authority; return known; }
static bool exp3_board_runtime(uint32_t,uint16_t *word) { *word=runtime; return known; }
static bool batteryKnown;
static uint16_t batteryMin, batteryReasons;
static bool exp3_board_battery(uint32_t,uint16_t *minimum,uint16_t *reasons) {
    *minimum=batteryMin; *reasons=batteryReasons; return batteryKnown;
}
static bool bagKnown, bagConnected;
static bool bagFullKnown, bagFull;
static bool heightKnown;
static uint16_t heightMm;
static bool exp3_board_height(uint32_t, uint16_t *mm) {
    *mm=heightMm; return heightKnown;
}
static bool exp3_board_bag_connection(uint32_t,bool *connected) {
    *connected=bagConnected; return bagKnown;
}
static bool exp3_board_bag_full(uint32_t,bool *full) {
    *full=bagFull; return bagFullKnown;
}
static bool has(const char *value) {
    for(const auto &label:labels) if(label==value) return true;
    return false;
}
static void resetDraw() { labels.clear(); textPositions.clear(); memset(pixels,0,sizeof(pixels)); }
static bool hasAt(const char *value,int x,int y) {
    for(const auto &position:textPositions)
        if(position.value==value && position.x==x && position.y==y) return true;
    return false;
}
static void assertModalBodyClean() {
    for(int y=14;y<51;++y) for(int x=0;x<128;++x)
        if(x<15 || x>114 || y<16 || y>49) assert(!pixels[y][x]);
}
static void saveScreen(const char *name) {
    const char *dir=std::getenv("MOWER_UI_PREVIEW_DIR");
    if(!dir) return;
    std::ofstream file(std::string(dir)+"/"+name+".pbm");
    file << "P1\n128 64\n";
    for(auto &row:pixels) { for(auto pixel:row) file << int(pixel) << ' '; file << '\n'; }
}
'''.replace('MOWER_HEADER', str(header)).replace('PROBE_HEADER', str(probe)).replace('BOARD_HEADER', str(root/'experiments/ticket03/exp3_board.h'))
tests = r'''
int main() {
    // Controller identities can begin at any token; no local edge count is used.
    board={};board.bound=true;board.controller=1;board.hmi=2;
    known=true;runtime=MOWER_FAST_VALID|MOWER_FAST_LOCKED;
    authority=mower_auth_pack(MOWER_AUTH_CENTER,true,57U);
    channelOutputs[3]=0;assert(mowerExp3Step(0)==EXP3_UI_IDLE);
    channelOutputs[3]=-1000;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_SYNCHRONIZING && submissions==0U);
    authority=mower_auth_pack(MOWER_AUTH_CONFIRM,true,58U);++ticks;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT && mowerUnlockDisplay);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==1U);
    assert(mowerUnlockProbe.pending.generation==58U);
    const Exp3Message pending=mowerUnlockProbe.pending;
    ticks+=201U;assert(mowerExp3Step(0)==EXP3_UI_WAIT && submissions==1U);
    board.last_result=pending;board.last_result.kind=EXP3_RESULT;
    board.last_result.command=EXP3_CONFIRM;board.last_result.status=EXP3_ACCEPTED;
    ++board.matches;
    assert(mowerExp3Step(0)==EXP3_UI_WAIT); // Actual locked feedback is not success.
    runtime=MOWER_FAST_VALID|MOWER_FAST_RUN|(1U<<MOWER_FAST_HEARTBEAT_SHIFT);
    authority=mower_auth_pack(MOWER_AUTH_CH4,false,58U);
    assert(mowerExp3Step(0)==EXP3_UI_IDLE && !mowerExp3Probe.start_armed);
    for(unsigned i=0;i<5U;++i)assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE);
    assert(submissions==1U);
    authority=mower_auth_pack(MOWER_AUTH_CENTER,false,58U);channelOutputs[3]=0;mowerExp3Step(0);
    board.waiting=false;mowerExp3Step(0);channelOutputs[3]=-1000;mowerExp3Step(0);
    authority=mower_auth_pack(MOWER_AUTH_CONFIRM,false,59U);++ticks;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT);
    assert(mowerExp3Probe.pending.context==EXP3_PWM_CONTEXT && mowerExp3Probe.pending.action>pending.action);
    // Rejection is visible; returning to center closes it and accepts a new token.
    board.last_result=mowerExp3Probe.pending;board.last_result.kind=EXP3_RESULT;
    board.last_result.command=EXP3_CONFIRM;board.last_result.status=EXP3_UNKNOWN;++board.matches;
    assert(mowerExp3Step(0)==EXP3_UI_REJECTED);
    channelOutputs[3]=0;authority=mower_auth_pack(MOWER_AUTH_CENTER,false,59U);
    mowerExp3Step(0);channelOutputs[3]=-1000;mowerExp3Step(0);
    authority=mower_auth_pack(MOWER_AUTH_CONFIRM,false,60U);++ticks;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT);
    // Field-specific instructions, including a persistent actual LOW guard.
    for(unsigned phase=0;phase<8U;++phase){
      mowerExp3Initialized=false;channelOutputs[3]=0;
      authority=mower_auth_pack(phase,false,60U);mowerExp3Step(0);channelOutputs[3]=-1000;
      const unsigned count=submissions;mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER));
      assert(submissions==count);
    }
    mowerExp3Initialized=false;channelOutputs[3]=0;
    known=true; runtime=15;
    mowerExp3DisplayState=EXP3_UI_ACCEPTED;
    renderFixture=true; fixture.telemetry_online=true;
    fixture.valid=MOWER_UI_SOC|MOWER_UI_FAULT|MOWER_UI_HEIGHT_FEEDBACK;
    fixture.soc_percent=82; fixture.radio_battery_100mv=78;
    fixture.radio_quality=100; fixture.height_deci_cm=70; fixture.bag_command=2;
    labels.clear(); mowerUserRender();
    assert(has("已连接") && hasAt("高",114,14) && hasAt("1.10",3,26) && has("m/s"));
    assert(hasAt("4000",68,26) && has("rpm"));
    assert(hasAt("设定",14,14) && hasAt("刀设定",78,14));
    assert(!has("行走设") && !has("割刀") && !has("输出") && !has("开"));
    assert(mowerZhTextWidth("刀设定")==33 && mowerZhTextWidth("1.80m/s")==42);
    assert(mowerZhTextWidth("按OK确认")==45);
    const uint16_t gradeWords[]={0,3,5,7};
    const char *gradeLabels[]={"关","低","中","高"};
    const char *gradeScreens[]={"blade-off","blade-low","blade-middle","blade-high"};
    const char *gradeRpm[]={"0","3000","3750","4000"};
    for(unsigned i=0;i<4;++i) {
      runtime=gradeWords[i];
      resetDraw(); mowerUserRender();
      assert(hasAt(gradeLabels[i],114,14));
      assert(hasAt(gradeRpm[i],68,26) && has("rpm")); saveScreen(gradeScreens[i]);
    }
    fixture.valid |= MOWER_UI_BLADE_RPM;
    fixture.blade_actual_rpm=2555;
    resetDraw(); mowerUserHome(fixture,true,5U);
    assert(hasAt("3750",68,26) && !has("2555"));
    resetDraw(); mowerUserDebugRow(5,12,fixture,true,5U);
    assert(has("2555")); // Diagnostics keep the raw feedback contract.
    resetDraw(); mowerUserHome(fixture,true,1U); // Enable flag without a legal gear.
    assert(hasAt("--",114,14) && hasAt("--",68,26));
    known=false; resetDraw(); mowerUserRender();
    assert(hasAt("--",114,14) && hasAt("--",68,26)); saveScreen("blade-unknown");
    fixture.valid &= ~MOWER_UI_BLADE_RPM;
    known=true; runtime=15; resetDraw(); mowerUserRender();
    assert(hasAt("高",14,40) && has("7.0cm") && !has("高度设") && !has("3/5"));
    fixture.valid |= MOWER_UI_BAG_CONNECTION;
    for(unsigned command=0; command<=2; ++command) {
      fixture.bag_command=command; fixture.bag_connected=false;
      resetDraw(); mowerUserRender(); assert(hasAt("未连接",78,40));
      assert(!hasAt("开",78,40) && !hasAt("关",78,40));
    }
    fixture.bag_connected=true;
    const char *bagLabels[]={"--","开","关"};
    for(unsigned command=0; command<=2; ++command) {
      fixture.bag_command=command;
      resetDraw(); mowerUserRender(); assert(hasAt(bagLabels[command],78,40));
    }
    fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); assert(hasAt("未知",78,40));
    fixture.telemetry_online=false; fixture.valid |= MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); assert(hasAt("未知",78,40));
    fixture.telemetry_online=true; fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    fixture.bag_command=2;
    fixture.valid |= MOWER_UI_BAG_CONNECTION | MOWER_UI_BAG_FULL;
    fixture.bag_connected=true; fixture.bag_full=false;
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("未满",104,40) && hasAt("关",78,40));
    saveScreen("bag-not-full");
    uint8_t notFullPixels[64][128]; memcpy(notFullPixels,pixels,sizeof(pixels));
    fixture.bag_full=true;
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("已满",104,40) && hasAt("关",78,40));
    for(int y=0;y<64;++y) for(int x=0;x<128;++x)
      if(x<104 || x>=126 || y<40 || y>=52) assert(pixels[y][x]==notFullPixels[y][x]);
    saveScreen("bag-full");
    uint8_t fullPixels[64][128]; memcpy(fullPixels,pixels,sizeof(pixels));
    resetDraw(); mowerZhText(104,40,"已满");
    for(int y=40;y<51;++y) for(int x=104;x<126;++x)
      assert(fullPixels[y][x] == !pixels[y][x]);
    resetDraw(); mowerUserDebugRow(28,12,fixture,true,15U);
    assert(has("草袋满载") && has("已满"));
    for(int condition=0;condition<4;++condition) {
      MowerUiSnapshot unknown=fixture;
      if(condition==0) unknown.telemetry_online=false;
      if(condition==1) unknown.valid &= ~MOWER_UI_BAG_FULL;
      if(condition==2) unknown.valid &= ~MOWER_UI_BAG_CONNECTION;
      if(condition==3) unknown.bag_connected=false;
      resetDraw(); mowerUserHome(unknown,true,15U);
      assert(!hasAt("未满",104,40) && !hasAt("已满",104,40));
      if(condition==1) assert(hasAt("--",104,40));
      else assert(!hasAt("--",104,40));
      saveScreen(condition==3 ? "bag-full-disconnected" : "bag-full-unknown");
      resetDraw(); mowerUserDebugRow(28,12,unknown,true,15U); assert(has("未知"));
    }
    fixture.valid &= ~(MOWER_UI_BAG_FULL | MOWER_UI_BAG_CONNECTION);
    fixture.bag_full=false;
    channelOutputs[9]=-1000; mowerUserRender(); assert(hasAt("0.40",3,26));
    channelOutputs[9]=1000; mowerUserRender(); assert(hasAt("1.80",3,26));
    channelOutputs[9]=-500; mowerUserRender(); assert(hasAt("0.75",3,26));
    channelOutputs[9]=500; mowerUserRender(); assert(hasAt("1.45",3,26));
    channelOutputs[9]=0;
    labels.clear(); runtime=8; mowerUserRender();
    assert(has("关") && !has("开"));
    labels.clear(); known=false; mowerUserRender();
    assert(hasAt("--",114,14));
    labels.clear(); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("确认启动割刀") && has("按OK确认"));
    labels.clear(); mowerExp3Overlay(EXP3_UI_REJECTED);
    assert(has("启动被拒绝") && !has("开"));

    // Compare all CH9 boundary/prior-state combinations against the unchanged
    // real chassis decoder, rather than duplicating expected mapping logic.
    for(unsigned prior=0; prior<=6; ++prior)
      for(unsigned pulse=900; pulse<=2100; ++pulse)
        assert(mowerUiHeightSetting(pulse,prior) == RemoteBackend_DecodeBladeHeight(nullptr,pulse,prior));
    assert(mowerUiHeightSetting(899,3)==0 && mowerUiHeightSetting(2101,3)==0);
    assert(mowerUiBagCommand(900)==1 && mowerUiBagCommand(1120)==1);
    assert(mowerUiBagCommand(1121)==0 && mowerUiBagCommand(1879)==0);
    assert(mowerUiBagCommand(1880)==2 && mowerUiBagCommand(2100)==2);
    assert(mowerUiBagCommand(899)==0 && mowerUiBagCommand(2101)==0);

    renderFixture=false;
    streaming=true;
    const uint16_t sensorIds[]={0x03,0x06,0x13,0x02,0x16,0x15,0xE0};
    const int sensorValues[]={4860,82,75,3200,1,0,0};
    for(unsigned i=0;i<7;++i) {
      g_model.telemetrySensors[i].id=sensorIds[i];
      telemetryItems[i].available=true; telemetryItems[i].value=sensorValues[i];
    }
    const char *modeLabels[]={"--","遥控","API待机","API运行","接管"};
    for(unsigned mode=0; mode<5; ++mode) {
      telemetryItems[4].value=mode;
      MowerUiSnapshot modeData; mowerUiReadLive(modeData);
      assert((modeData.valid & MOWER_UI_WORK_MODE) && modeData.work_mode==mode);
      modeData.valid |= MOWER_UI_BAG_CONNECTION;modeData.bag_connected=true;
      resetDraw();mowerUserHome(modeData,true,8);
      assert(hasAt(modeLabels[mode],56-mowerZhTextWidth(modeLabels[mode])/2,0));
      if(mode==2 || mode==3) {
        assert(has("实速") && hasAt("0.75",3,26) && hasAt("API",78,40));
        resetDraw(); mowerUserHome(modeData,true,MOWER_FAST_VALID|7U);
        assert(hasAt("4000",68,26) && hasAt("高",114,14));
        assert(!has("设定") && !hasAt("开",78,40) && !hasAt("关",78,40));
      }
      saveScreen(mode==2?"mode-api-standby":mode==3?"mode-api-running":mode==4?"mode-takeover":"mode-remote");
      modeData.valid &= ~MOWER_UI_WORK_MODE;resetDraw();mowerUserHome(modeData,true,8);
      assert(hasAt("--",50,0));
    }
    telemetryItems[4].value=5;MowerUiSnapshot invalidMode;mowerUiReadLive(invalidMode);
    assert(!(invalidMode.valid & MOWER_UI_WORK_MODE));
    telemetryItems[4].value=1;
    MowerUiSnapshot live;
    channelOutputs[8]=-1000; channelOutputs[5]=1000;
    mowerUiReadLive(live);
    assert(live.telemetry_online && live.soc_percent==82 && !(live.valid & MOWER_UI_HEIGHT_FEEDBACK) && live.bag_command==2);
    assert(!(live.valid & MOWER_UI_BAG_CONNECTION)); // Old chassis: unknown.
    bagKnown=true; bagConnected=false;
    const int bagOutputs[]={-1000,1000,0,-1000,1000};
    for(auto output:bagOutputs) {
      channelOutputs[5]=output;
      mowerUiReadLive(live);
      assert((live.valid & MOWER_UI_BAG_CONNECTION) && !live.bag_connected);
      renderFixture=true; fixture=live;
      resetDraw(); mowerUserRender(); assert(hasAt("未连接",78,40));
    }
    bagConnected=true;
    for(auto output:bagOutputs) {
      channelOutputs[5]=output; mowerUiReadLive(live); fixture=live;
      assert((live.valid & MOWER_UI_BAG_CONNECTION) && live.bag_connected);
      resetDraw(); mowerUserRender();
      assert(hasAt(output < 0 ? "开" : output > 0 ? "关" : "--",78,40));
    }
    bagKnown=false; mowerUiReadLive(live); fixture=live;
    resetDraw(); mowerUserRender(); assert(hasAt("未知",78,40));
    bagKnown=true; bagConnected=true; bagFullKnown=true; bagFull=true;
    mowerUiReadLive(live);
    assert((live.valid & MOWER_UI_BAG_FULL) && live.bag_full);
    bagFull=false; mowerUiReadLive(live);
    assert((live.valid & MOWER_UI_BAG_FULL) && !live.bag_full);
    bagFullKnown=false; mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_BAG_FULL));
    bagFullKnown=true; bagConnected=false;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_BAG_FULL));
    bagConnected=true; streaming=false;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_BAG_FULL));
    streaming=true; bagKnown=false; bagFullKnown=false;
    renderFixture=false;
    channelCenters[8]=1550; channelOutputs[8]=900; // transmitted pulse=2000
    channelCenters[5]=1550; channelOutputs[5]=-1100; // transmitted pulse=1000
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_HEIGHT_FEEDBACK) && live.bag_command==1);
    channelCenters[8]=1500; channelCenters[5]=1500;
    channelOutputs[8]=1000; channelOutputs[5]=-1000; // model reversal is already in channelOutputs
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_HEIGHT_FEEDBACK) && live.bag_command==1);
    channelCenters[9]=1600; channelOutputs[9]=-1200; assert(mowerTravelSpeedCms()==40);
    channelOutputs[9]=800; assert(mowerTravelSpeedCms()==180);
    channelCenters[9]=1500; channelOutputs[9]=0;
    channelOutputs[8]=2000; channelOutputs[5]=0;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_HEIGHT_FEEDBACK) && live.bag_command==0);
    // Display follows the received chassis estimate, never local CH9.
    heightKnown=true; heightMm=70;
    for (int output: {-1000,0,1000,2000}) {
      channelOutputs[8]=output; mowerUiReadLive(live);
      assert((live.valid & MOWER_UI_HEIGHT_FEEDBACK) && live.height_deci_cm==70);
      renderFixture=true; fixture=live; resetDraw(); mowerUserRender();
      assert(has("7.0cm") && !has("高度设") && !has("3/5"));
    }
    const unsigned calibratedMm[] = {20,40,60,70,80};
    const char *expectedHeight[] = {"2.0cm","4.0cm","6.0cm","7.0cm","8.0cm"};
    for (unsigned i=0; i<5; ++i) {
      heightMm=calibratedMm[i]; mowerUiReadLive(live); fixture=live;
      resetDraw(); mowerUserRender(); assert(has(expectedHeight[i]));
    }
    heightMm=110; mowerUiReadLive(live); fixture=live;
    fixture.valid |= MOWER_UI_BAG_CONNECTION; fixture.bag_connected=false;
    resetDraw(); mowerUserRender(); assert(has("11.0cm") && hasAt("未连接",78,40));
    saveScreen("height-11cm-bag-disconnected");
    heightKnown=false; mowerUiReadLive(live); fixture=live;
    resetDraw(); mowerUserRender(); assert(!has("11.0cm") && !has("7.0cm") && has("--cm"));
    saveScreen("height-unknown");
    renderFixture=false;
    // Receiver RSSI alone cannot claim chassis connection.
    known=false;
    for(unsigned i=4;i<7;++i) telemetryItems[i].old=true;
    mowerUiReadLive(live); assert(!live.telemetry_online);
    telemetryItems[4].old=false;
    mowerUiReadLive(live); assert(live.telemetry_online && !(live.valid & MOWER_UI_FAULT));
    assert(std::string(mowerConnection(live))=="未知");
    assert(!(live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)));
    telemetryItems[6].old=false; telemetryItems[6].value=0x1C0;
    mowerUiReadLive(live);
    assert(!(live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)));
    telemetryItems[6].value=0; mowerUiReadLive(live);
    assert((live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)) ==
        (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM));
    telemetryItems[6].value=0x30F;
    mowerUiReadLive(live); assert(std::string(mowerConnection(live))=="碰撞");
    g_model.telemetrySensors[7].id=0xE0; telemetryItems[7].available=true;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_FAULT));
    telemetryItems[7].available=false;
    telemetryItems[1].value=101; mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_SOC));
    streaming=false; mowerUiReadLive(live);
    assert(!live.valid && !live.telemetry_online && live.radio_battery_100mv==78);
    bagKnown=true; mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_BAG_CONNECTION));
    bagKnown=false; heightKnown=true; heightMm=70;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_HEIGHT_FEEDBACK));
    heightKnown=false;

    // Exercise the production main-view router with real probe state.
    renderFixture=true; fixture.valid=MOWER_UI_SOC|MOWER_UI_FAULT;
    fixture.telemetry_online=true; fixture.fault_bits=0;
    board={}; board.bound=true; board.controller=1; board.hmi=2;
    mowerExp3Initialized=false; known=true; runtime=8; submissions=0;
    authority=mower_auth_pack(MOWER_AUTH_CENTER,false,60U);
    channelOutputs[3]=0; mowerUserPage=MOWER_USER_HOME;
    g_eeGeneral.view=7;
    menuMainView(0); assert(g_eeGeneral.view==VIEW_MOWER);
    menuMainView(EVT_KEY_BREAK(KEY_DOWN)); assert(mowerUserPage==MOWER_USER_HOME);
    menuMainView(EVT_KEY_BREAK(KEY_LEFT)); assert(mowerUserPage==MOWER_USER_HOME);
    menuMainView(EVT_KEY_LONG(KEY_RIGHT));
    assert(mowerUserPage==MOWER_USER_MAINTENANCE && killedKey==KEY_RIGHT);
    deliverKey(EVT_KEY_BREAK(KEY_RIGHT)); // keyboard kills the long-press release
    assert(mowerUserPage==MOWER_USER_MAINTENANCE && submissions==0);
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(mowerUserPage==MOWER_USER_DEBUG && submissions==0);
    for(unsigned i=0;i<100;++i) menuMainView(EVT_KEY_REPT(KEY_DOWN));
    assert(mowerDebugFirstRow==MOWER_DEBUG_ROWS-4);
    for(unsigned i=0;i<100;++i) menuMainView(EVT_KEY_REPT(KEY_UP));
    assert(mowerDebugFirstRow==0);
    menuMainView(EVT_KEY_BREAK(KEY_EXIT)); assert(mowerUserPage==MOWER_USER_MAINTENANCE);
    menuMainView(EVT_KEY_BREAK(KEY_DOWN));
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(pushedMenu==11 && submissions==0);
    menuMainView(EVT_KEY_BREAK(KEY_DOWN));
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(pushedMenu==12 && submissions==0);
    menuMainView(EVT_KEY_BREAK(KEY_EXIT)); assert(mowerUserPage==MOWER_USER_HOME);
    menuMainView(EVT_KEY_LONG(KEY_MENU));
    assert(killedKey==KEY_MENU); deliverKey(EVT_KEY_BREAK(KEY_MENU)); assert(submissions==0);
    mowerUserPage=MOWER_USER_MAINTENANCE;
    channelOutputs[3]=-1024;
    channelOutputs[3]=0; mowerExp3DisplayState=EXP3_UI_IDLE;
    // Navigation OK is consumed even if a valid physical request appears on
    // that frame; background/menu polling cannot authorize it.
    mowerExp3Initialized=false;board.waiting=false;submissions=0;known=true;
    runtime=MOWER_FAST_VALID|MOWER_FAST_RUN|(1U<<MOWER_FAST_HEARTBEAT_SHIFT);
    authority=mower_auth_pack(MOWER_AUTH_CENTER,false,100U);
    channelOutputs[3]=0;mowerUserPage=MOWER_USER_MAINTENANCE;menuMainView(0);
    channelOutputs[3]=-1000;authority=mower_auth_pack(MOWER_AUTH_CONFIRM,false,101U);
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(submissions==0U && mowerUserPage==MOWER_USER_HOME);
    assert(mowerExp3Probe.state==EXP3_UI_PROMPT);
    mowerExp3Step(0);assert(submissions==0U);
    menuMainView(EVT_KEY_BREAK(KEY_EXIT));assert(submissions==0U);
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));assert(submissions==1U);
    channelOutputs[3]=0;

    // Pixel snapshots use the actual checked-in glyphs and production drawers.
    mowerExp3DisplayState=EXP3_UI_IDLE; mowerExp3Probe.state=EXP3_UI_IDLE;
    mowerExp3Probe.cancel_pending=false; mowerExp3CancelFailed=false;
    mowerUserPage=MOWER_USER_HOME; runtime=15; fixture.height_deci_cm=70; fixture.bag_command=2;
    fixture.valid |= MOWER_UI_HEIGHT_FEEDBACK;
    resetDraw(); mowerUserRender(); saveScreen("home-online");
    fixture.valid |= MOWER_UI_BAG_CONNECTION; fixture.bag_connected=false;
    resetDraw(); mowerUserRender(); saveScreen("bag-disconnected"); assert(hasAt("未连接",78,40));
    fixture.bag_connected=true; fixture.bag_command=1;
    resetDraw(); mowerUserRender(); saveScreen("bag-open"); assert(hasAt("开",78,40));
    fixture.bag_command=2;
    resetDraw(); mowerUserRender(); saveScreen("bag-closed"); assert(hasAt("关",78,40));
    fixture.bag_command=0;
    resetDraw(); mowerUserRender(); saveScreen("bag-command-unknown"); assert(hasAt("--",78,40));
    fixture.bag_command=2; fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); saveScreen("bag-unknown"); assert(hasAt("未知",78,40));
    fixture.telemetry_online=false; fixture.valid=0;
    resetDraw(); mowerUserRender(); saveScreen("home-offline");
    assert(has("未连接") && has("--") && !has("100") && !hasAt("高",114,14) && !has("开"));
    fixture.telemetry_online=true; fixture.fast_status_known=false; fixture.valid=MOWER_UI_FAULT|MOWER_UI_SOC; fixture.fault_bits=0x30F;
    resetDraw(); mowerUserRender(); saveScreen("home-fault"); assert(has("碰撞"));
    mowerUserPage=MOWER_USER_MAINTENANCE;
    resetDraw(); mowerUserRender(); saveScreen("maintenance");
    mowerUserPage=MOWER_USER_DEBUG;
    bool sawBladeExpiry=false, sawBladeCrc=false, sawFault=false;
    for(unsigned first=0;first<MOWER_DEBUG_ROWS;first+=4) {
      mowerDebugFirstRow=first > MOWER_DEBUG_ROWS-4 ? MOWER_DEBUG_ROWS-4 : first;
      resetDraw(); mowerUserRender();
      sawBladeExpiry |= has("割刀反馈过期"); sawBladeCrc |= has("割刀校验异常"); sawFault |= has("!");
      std::string name="debug-"+std::to_string(first); saveScreen(name.c_str());
    }
    assert(sawBladeExpiry && sawBladeCrc && sawFault);
    mowerUserPage=MOWER_USER_HOME; mowerExp3DisplayState=EXP3_UI_PROMPT;
    resetDraw(); mowerUserRender(); saveScreen("confirm");
    mowerExp3Probe.cancel_pending=true; mowerExp3Probe.state=EXP3_UI_CANCEL_WAIT;
    mowerExp3Probe.sent_at=ticks*10;
    resetDraw(); mowerUserRender(); saveScreen("stopping");
    ticks+=1001; resetDraw(); mowerUserRender(); saveScreen("stop-no-reply");
    for(auto state:{EXP3_UI_WAIT,EXP3_UI_CANCEL_WAIT,EXP3_UI_TIMEOUT,EXP3_UI_UNBOUND,EXP3_UI_REJECTED}) {
      resetDraw(); mowerExp3Overlay(state);
    }

    // An old healthy E0 must not keep actual speed valid after fast wheel expiry.
    renderFixture=false; streaming=true; known=true; runtime=MOWER_FAST_VALID|MOWER_FAST_RUN|MOWER_FAST_WHEEL_STALE;
    telemetryItems[6].available=true; telemetryItems[6].old=false; telemetryItems[6].value=0U;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_WALK_ACTUAL));
    renderFixture=true; runtime=MOWER_FAST_VALID|MOWER_FAST_RUN;
    // Startup lock precedes a pending BMS sample; no connection still stays offline.
    const MowerUiSnapshot previousFixture=fixture;
    fixture={}; fixture.telemetry_online=false;
    assert(std::string(mowerConnection(fixture))=="未连接");
    fixture.telemetry_online=true;
    assert(std::string(mowerConnection(fixture))=="未知");
    fixture.valid=MOWER_UI_FAULT; fixture.fault_bits=0x440U;
    fixture.fast_status_known=true;
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_LOCKED|MOWER_FAST_FAULT;
    assert(std::string(mowerConnection(fixture))=="锁定");
    resetDraw(); mowerUserHome(fixture,true,fixture.fast_status); saveScreen("startup-locked-bms-pending");
    fixture=previousFixture;
    // Fast homepage state wins over conflicting slow diagnostic E0 values.
    fixture.telemetry_online=true; fixture.fast_status_known=true;
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_RUN; fixture.fault_bits=0x400U;
    assert(std::string(mowerConnection(fixture))=="已连接");
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_LOCKED; fixture.fault_bits=0U;
    assert(std::string(mowerConnection(fixture))=="锁定");
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_COLLISION;
    assert(std::string(mowerConnection(fixture))=="碰撞");
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_LOCKED|0x8000U;
    fixture.fault_bits=0U; // E0 may not have reached the transmitter yet.
    assert(std::string(mowerConnection(fixture))=="急停");
    resetDraw(); mowerUserHome(fixture,true,fixture.fast_status); saveScreen("status-estop-fast");
    fixture.fast_status|=MOWER_FAST_COLLISION;
    assert(std::string(mowerConnection(fixture))=="碰撞");
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_RUN;
    fixture.fault_bits=0x403U; // Old E0 cannot resurrect either released cause.
    assert(std::string(mowerConnection(fixture))=="已连接");
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_LOCKED;
    assert(std::string(mowerConnection(fixture))=="锁定");
    fixture.fast_status_known=false;
    // Wheel expiry is data validity, not a user alarm. Other faults still alarm.
    fixture={}; fixture.telemetry_online=true; fixture.valid=MOWER_UI_FAULT;
    fixture.fault_bits=0;
    resetDraw(); mowerUserHome(fixture,false,0);
    uint8_t healthyPixels[64][128]; memcpy(healthyPixels,pixels,sizeof(pixels));
    fixture.fault_bits=0x80U;
    assert(std::string(mowerConnection(fixture))=="已连接");
    resetDraw(); mowerUserHome(fixture,false,0);
    assert(has("已连接") && !has("报错") && !memcmp(healthyPixels,pixels,sizeof(pixels)));
    for(unsigned bit=0;bit<10;++bit) {
      const unsigned mask=1U<<bit;
      if(mask==0x80U) continue;
      fixture.fault_bits=0x80U|mask;
      assert(std::string(mowerConnection(fixture))==(mask==1U ? "急停" : mask==2U ? "碰撞" : mask==0x40U ? "已连接" : "报错"));
    }
    fixture.fault_bits=0x80U;
    resetDraw();
    for(unsigned row=0;row<MOWER_DEBUG_ROWS;++row)
      mowerUserDebugRow(row,12,fixture,false,0);
    assert(!has("轮速反馈过期") && has("割刀反馈过期") && has("系统锁定"));
    assert(has("0x0080")); // raw diagnostic retained

    // Restored home battery row; pack voltage remains on the debug page.
    fixture={}; fixture.telemetry_online=true;
    fixture.valid=MOWER_UI_FAULT|MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_BATTERY_MIN|MOWER_UI_BATTERY_REASON;
    fixture.soc_percent=100; fixture.voltage_centivolts=5200;
    fixture.battery_min_mv=2930; fixture.battery_reasons=4;
    fixture.radio_battery_100mv=78;
    resetDraw(); mowerUserHome(fixture,false,0);
    assert(hasAt("100%",35,52) && hasAt("7.8V",98,52) && pixels[58][28]);
    assert(!has("BMS估算") && !has("保护已标定") && !has("保护待标定") && !has("保护未知") && !has("52.00V"));
    saveScreen("battery-home-restored");
    resetDraw(); mowerUserDebugRow(2,12,fixture,false,0); assert(has("52.00V"));
    fixture.soc_percent=29; fixture.voltage_centivolts=4700;
    fixture.fault_bits=MOWER_BATTERY_WARNING|MOWER_BATTERY_BLADE_INHIBIT;
    resetDraw(); mowerUserHome(fixture,false,0);
    assert(has("请充电") && has("29%") && !has("47.00V")); saveScreen("battery-low");
    resetDraw(); mowerUserDebugRow(26,12,fixture,false,0);
    mowerUserDebugRow(27,24,fixture,false,0);
    assert(has("2.930V") && has("电压过低"));
    fixture.battery_reasons=0x20;
    resetDraw(); mowerUserDebugRow(27,24,fixture,false,0); assert(has("电量过低"));
    fixture.valid &= ~(MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_BATTERY_MIN);
    fixture.fault_bits=0x40U;
    resetDraw(); mowerUserHome(fixture,false,0);
    assert(has("已连接") && has("--%") && !has("电池异常") && !has("请充电") && !has("29%")); saveScreen("battery-invalid");
    fixture.battery_reasons=1;
    resetDraw(); mowerUserDebugRow(22,12,fixture,false,0);
    mowerUserDebugRow(27,24,fixture,false,0);
    assert(has("电池反馈异常") && has("!") && has("数据失效"));
    saveScreen("battery-invalid-debug");
    fixture.fault_bits=MOWER_BATTERY_WARNING|MOWER_BATTERY_BLADE_INHIBIT|0x40U;
    resetDraw(); mowerUserHome(fixture,false,0);
    assert(has("请充电") && has("--%")); // Confirmed LOW survives lost BMS feedback.

    authority=mower_auth_pack(MOWER_AUTH_LOW,false,60U);
    runtime=MOWER_FAST_VALID|MOWER_FAST_RUN;known=true;mowerExp3Initialized=false;
    board={};board.bound=true;board.controller=1;board.hmi=2;
    channelOutputs[3]=0;mowerExp3Step(0);channelOutputs[3]=-1000;
    const unsigned count=submissions;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_BATTERY_LOW && submissions==count);
    mowerExp3DisplayState=EXP3_UI_BATTERY_LOW;resetDraw();mowerUserRender();saveScreen("battery-start-denied");
    // Every modal keeps the complete header and clears whole background rows.
    // Centering expectations use the approved 11px CJK / 6px ASCII dimensions.
    mowerExp3Probe.cancel_pending=false; mowerExp3Probe.state=EXP3_UI_IDLE;
    mowerExp3CancelFailed=false;
    for(bool unlock:{false,true}) {
      mowerUnlockDisplay=unlock;
      for(auto state:{EXP3_UI_PROMPT,EXP3_UI_WAIT,EXP3_UI_CANCEL_WAIT,
                      EXP3_UI_TIMEOUT,EXP3_UI_UNBOUND,EXP3_UI_REJECTED}) {
        resetDraw(); mowerUserHome(fixture,true,15);
        uint8_t headerPixels[12][128]; memcpy(headerPixels,pixels,sizeof(headerPixels));
        mowerExp3Overlay(state);
        assert(!memcmp(headerPixels,pixels,sizeof(headerPixels)));
        if(state==EXP3_UI_PROMPT) assertModalBodyClean();
        if(state==EXP3_UI_PROMPT) {
          assert(hasAt(unlock ? "是否解锁" : "确认启动割刀",unlock ? 42 : 31,18));
          assert(hasAt("按OK确认",42,34));
        }
        if(state==EXP3_UI_TIMEOUT) assert(hasAt("无回执",48,52));
        if(state==EXP3_UI_UNBOUND) assert(!has("链路未就绪"));
      }
    }

    // Accepted alone cannot wait forever for missing/locked runtime.
    board={};board.bound=true;board.controller=3U;board.hmi=4U;
    known=true;runtime=MOWER_FAST_VALID|MOWER_FAST_LOCKED;mowerExp3Initialized=false;
    authority=mower_auth_pack(MOWER_AUTH_CENTER,true,1U);channelOutputs[3]=0;mowerExp3Step(0);
    channelOutputs[3]=-1000;authority=mower_auth_pack(MOWER_AUTH_CONFIRM,true,2U);mowerExp3Step(0);
    mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER));
    board.last_result=mowerUnlockProbe.pending;board.last_result.kind=EXP3_RESULT;
    board.last_result.command=EXP3_CONFIRM;board.last_result.status=EXP3_ACCEPTED;++board.matches;
    assert(mowerExp3Step(0)==EXP3_UI_WAIT);ticks+=1000U;
    assert(mowerExp3Step(0)==EXP3_UI_TIMEOUT && !board.bound);
    // Recenter closes a timeout and keeps maintenance available while offline.
    channelOutputs[3]=0;known=false;mowerUserPage=MOWER_USER_HOME;
    menuMainView(0);menuMainView(EVT_KEY_LONG(KEY_RIGHT));
    assert(mowerUserPage==MOWER_USER_MAINTENANCE);

    const Exp3UiState phaseStates[]={EXP3_UI_NOT_READY,EXP3_UI_STICKS_REQUIRED,
      EXP3_UI_CENTER_REQUIRED,EXP3_UI_SYNCHRONIZING,EXP3_UI_SAFETY,EXP3_UI_OWNER};
    const char *phaseNames[]={"startup-init","startup-sticks","startup-ch4","startup-sync","startup-safety","startup-owner"};
    for(unsigned i=0;i<6U;++i){
      resetDraw();mowerExp3Overlay(phaseStates[i]);
      if(phaseStates[i]==EXP3_UI_STICKS_REQUIRED || phaseStates[i]==EXP3_UI_CENTER_REQUIRED) {
        assert(!has("行走摇杆请回中") && !has("CH4请回中"));
      }
      saveScreen(phaseNames[i]);
    }
    mowerUnlockDisplay=true;
    resetDraw();mowerExp3Overlay(EXP3_UI_PROMPT);saveScreen("unlock-confirm");
    resetDraw();mowerExp3Overlay(EXP3_UI_REJECTED);saveScreen("unlock-rejected");
    // A background receipt wait must never force maintenance back home.
    board={};board.bound=true;board.controller=17U;board.hmi=18U;
    known=true;runtime=MOWER_FAST_VALID|MOWER_FAST_LOCKED;
    authority=mower_auth_pack(MOWER_AUTH_CONFIRM,true,2U);
    mowerExp3Initialized=false;channelOutputs[3]=0;mowerExp3Step(0);
    mowerUnlockProbe.state=EXP3_UI_WAIT;
    mowerUnlockProbe.pending.action=1U;
    mowerUnlockProbe.sent_at=ticks*10U;
    mowerUnlockProbe.seen_matches=board.matches;
    channelOutputs[3]=-1000;board.waiting=true;
    mowerUserPage=MOWER_USER_MAINTENANCE;
    menuMainView(0);
    assert(mowerUserPage==MOWER_USER_MAINTENANCE);
    // Shared fixed-point conversion must preserve zero padding and full unsigned range.
    resetDraw(); mowerUserValue(0,12,0,true,"V",3); assert(has("0.000V"));
    resetDraw(); mowerUserValue(0,12,12,true,"V",3); assert(has("0.012V"));
    resetDraw(); mowerUserValue(0,12,65535,true,"V",3); assert(has("65.535V"));
    resetDraw(); mowerUserValue(0,12,0xffffffffU,true); assert(has("4294967295"));
    resetDraw(); mowerUserValue(0,12,0,false,"m/s",2); assert(has("--m/s"));
    // Work mode, speed and blade setpoint on the production home page.
    board={}; board.bound=true; board.controller=31U; board.hmi=32U;
    mowerExp3Initialized=false; known=true; runtime=MOWER_FAST_VALID;
    authority=mower_auth_pack(MOWER_AUTH_CH4,false,3U); channelOutputs[3]=0;
    fixture={}; fixture.telemetry_online=true; fixture.fast_status_known=true;
    fixture.valid=MOWER_UI_FAULT|MOWER_UI_SOC|MOWER_UI_HEIGHT_FEEDBACK|
        MOWER_UI_BAG_CONNECTION|MOWER_UI_BAG_FULL|MOWER_UI_WORK_MODE;
    fixture.fault_bits=MOWER_ULTRASONIC_ENABLED; fixture.soc_percent=82;
    fixture.height_deci_cm=70; fixture.bag_connected=true; fixture.bag_command=2; fixture.work_mode=1;
    fixture.radio_quality=100; fixture.radio_battery_100mv=78;
    renderFixture=true; mowerUserPage=MOWER_USER_DEBUG; mowerDebugFirstRow=0;
    const unsigned settingsSubmissions=submissions;
    menuMainView(0);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_ON_US && !mowerUltrasonicPending);
#endif
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("4000",68,26) && has("rpm"));
    resetDraw(); mowerUserHome(fixture,true,5U);
    assert(hasAt("3750",68,26) && hasAt("中",114,14)); saveScreen("dashboard-a-work");
    fixture.valid |= MOWER_UI_WALK_ACTUAL;
    fixture.walk_actual_cms=65535; fixture.work_mode=3;
    fixture.height_deci_cm=110; fixture.soc_percent=100; fixture.radio_battery_100mv=255;
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("655.35",3,26) && hasAt("4000",68,26));
    assert(hasAt("11.0cm",26,40) && hasAt("100%",35,52) && hasAt("25.5V",98,52));
    fixture.height_deci_cm=70; fixture.soc_percent=82; fixture.radio_battery_100mv=78;
    fixture.walk_actual_cms=0;
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("0.00",3,26));
    fixture.valid &= ~MOWER_UI_WALK_ACTUAL;
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("--",3,26));
    fixture.work_mode=1;
    resetDraw(); mowerUserHome(fixture,true,15U);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    assert(hasAt("避障",78,0) && hasAt("开",100,0) && hasAt("高",114,14));
    assert(!has("信") && !hasAt("100",78,0) && hasAt("刀设定",78,14)); saveScreen("ultrasonic-on");
    assert(std::string(mowerConnection(fixture))=="已连接");
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_OFF_US && mowerUltrasonicPending);
    assert(submissions==settingsSubmissions && has("待生效"));
    resetDraw(); mowerUserRender(); saveScreen("ultrasonic-pending");
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_OFF_US);
    resetDraw(); mowerUserHome(fixture,true,15U); assert(hasAt("开",100,0));
    ticks+=300U; resetDraw(); mowerUserRender(); assert(has("未确认"));
    saveScreen("ultrasonic-unconfirmed");
    fixture.fault_bits=0U; resetDraw(); mowerUserRender();
    assert(!mowerUltrasonicPending && hasAt("关",54,12)); saveScreen("ultrasonic-debug-off");
    resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("关",100,0)); saveScreen("ultrasonic-off");
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_ON_US && mowerUltrasonicPending);
    fixture.fault_bits=MOWER_ULTRASONIC_ENABLED; resetDraw(); mowerUserRender();
    assert(!mowerUltrasonicPending && hasAt("开",54,12)); saveScreen("ultrasonic-debug-on");
    // A passive warning never hides or supplies an authorization prompt.
    mowerUserPage=MOWER_USER_HOME; mowerExp3DisplayState=EXP3_UI_IDLE;
    fixture.fault_bits=MOWER_ULTRASONIC_ENABLED|MOWER_ULTRASONIC_NEAR;
    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_RUN;
    resetDraw(); mowerUserHome(fixture,true,runtime);
    uint8_t powerRow[12][128]; memcpy(powerRow,pixels[52],sizeof(powerRow));
    resetDraw(); mowerUserRender();
    assert(!memcmp(powerRow,pixels[52],sizeof(powerRow)));
    assert(hasAt("前方障碍",47,18) && hasAt("请倒车",53,34));
    assert(!has("按OK确认") && std::string(mowerConnection(fixture))=="已连接");
    assertModalBodyClean();saveScreen("obstacle-warning");
    mowerExp3DisplayState=EXP3_UI_PROMPT;mowerUnlockDisplay=true;
    resetDraw();mowerUserRender();
    assert(has("是否解锁") && has("按OK确认") && !has("前方障碍"));
    saveScreen("obstacle-unlock-priority");
    mowerUnlockDisplay=false;resetDraw();mowerUserRender();
    assert(has("确认启动割刀") && !has("前方障碍"));saveScreen("obstacle-blade-priority");
    mowerExp3DisplayState=EXP3_UI_TIMEOUT;resetDraw();mowerUserRender();
    assert(has("前方障碍") && hasAt("无回执",48,52));
    mowerExp3DisplayState=EXP3_UI_IDLE;
    for(unsigned absent=0;absent<5;++absent) {
      MowerUiSnapshot saved=fixture;
      if(absent==0) fixture.fault_bits &= ~MOWER_ULTRASONIC_NEAR;
      if(absent==1) fixture.fault_bits &= ~MOWER_ULTRASONIC_ENABLED;
      if(absent==2) fixture.telemetry_online=false;
      if(absent==3) fixture.fast_status_known=false;
      if(absent==4) fixture.valid &= ~MOWER_UI_FAULT;
      resetDraw();mowerUserRender();assert(!has("前方障碍") && !has("请倒车"));
      fixture=saved;
    }
    for(auto page:{MOWER_USER_DEBUG,MOWER_USER_MAINTENANCE}) {
      mowerUserPage=page;resetDraw();mowerUserRender();assert(!has("前方障碍"));
    }
    mowerUserPage=MOWER_USER_DEBUG;mowerDebugFirstRow=0;
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_OFF_US && submissions==settingsSubmissions);
    fixture.fault_bits=0;resetDraw();mowerUserRender();assert(!mowerUltrasonicPending);
    fixture.fault_bits=MOWER_ULTRASONIC_ENABLED;g_mower_ultrasonic_request=MOWER_ULTRASONIC_ON_US;
    puts("PASS front-obstacle popup, automatic clear/off/stale, auth priority, editable debug and restored blade/signal layout");
    // Faults and missing feedback must not be mistaken for a disabled setting.
    fixture.fast_status_known=false; resetDraw(); mowerUserHome(fixture,true,15U);
    assert(hasAt("--",100,0)); saveScreen("ultrasonic-unknown");
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_ON_US && !mowerUltrasonicPending);
    fixture.fast_status_known=true; fixture.valid=0;
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(!mowerUltrasonicPending);
    fixture.valid=MOWER_UI_FAULT; fixture.telemetry_online=false;
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(!mowerUltrasonicPending);
    fixture.telemetry_online=true; mowerDebugFirstRow=1;
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(!mowerUltrasonicPending);
    mowerDebugFirstRow=0;
    g_mower_ultrasonic_request=MOWER_ULTRASONIC_OFF_US;
    mowerUltrasonicPending=true; ++board.controller;
    menuMainView(0);
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_ON_US && !mowerUltrasonicPending);
    // A new authorization prompt consumes the view change, never the debug OK.
    channelOutputs[3]=0; authority=mower_auth_pack(MOWER_AUTH_CENTER,false,4U); menuMainView(0);
    channelOutputs[3]=-1000; authority=mower_auth_pack(MOWER_AUTH_CONFIRM,false,5U);
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(mowerUserPage==MOWER_USER_HOME && submissions==settingsSubmissions);
    assert(g_mower_ultrasonic_request==MOWER_ULTRASONIC_ON_US);
    puts("PASS ultrasonic home actual/unknown, debug-only toggle, pending/timeout/late feedback, session reset, OK isolation");
#else
    resetDraw();
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(mowerUserPage==MOWER_USER_DEBUG && submissions==settingsSubmissions);
    assert(has("超声波") && hasAt("停用",54,12) && !has("待生效"));
    saveScreen("ultrasonic-disabled-debug");

    mowerUserPage=MOWER_USER_HOME;
    mowerExp3DisplayState=EXP3_UI_IDLE;
    fixture.fault_bits=0;
    resetDraw(); mowerUserRender();
    assert(!has("避障") && !has("前方障碍") && !has("请倒车"));
    assert(has("已连接") && has("遥控") && has("刀设定") && has("82%"));
    assert(pixels[9][115] && pixels[9][124]); // Signal bars retained.
    uint8_t offScreen[64][128]; memcpy(offScreen,pixels,sizeof(offScreen));
    fixture.fault_bits=MOWER_ULTRASONIC_ENABLED|MOWER_ULTRASONIC_NEAR;
    resetDraw(); mowerUserRender();
    assert(!has("避障") && !has("前方障碍") && !has("请倒车"));
    assert(memcmp(offScreen,pixels,sizeof(offScreen))==0);
    saveScreen("ultrasonic-disabled-home");

    fixture.fast_status=MOWER_FAST_VALID|MOWER_FAST_LOCKED|MOWER_FAST_EMERGENCY;
    resetDraw(); mowerUserRender();
    assert(has("急停") && !has("前方障碍"));
    mowerExp3DisplayState=EXP3_UI_PROMPT; mowerUnlockDisplay=true;
    resetDraw(); mowerUserRender();
    assert(has("是否解锁") && has("按OK确认") && !has("前方障碍"));
    mowerUnlockDisplay=false;
    resetDraw(); mowerUserRender();
    assert(has("确认启动割刀") && has("按OK确认") && !has("前方障碍"));
    puts("PASS disabled UI: no sonar badge or popup even with both old E0 bits, debug read-only, emergency and CH4/OK prompts retained");
#endif
    puts("PASS profile-3 actual GUI: arbitrary controller token, early OK, explicit rejection, ACK+runtime success, independent blade, no 2s hide; home/debug/router/glyph/pixel checks");
}
'''
with tempfile.TemporaryDirectory(prefix='blade-ok-ui-') as work:
    work = Path(work)
    cpp = work/'ui.cpp'
    declarations = text[text.index('static Exp3UiProbe mowerExp3Probe;'):text.index('static Exp3UiState mowerExp3Step(')]
    native_lcd = (source.parent/'lcd.cpp').read_text()
    user = (source.parent/'mower_user_ui.h').read_text()
    user = user.replace('#pragma once', '').replace('#include "mower_zh_font.h"', '#include "'+str(source.parent/'mower_zh_font.h')+'"')
    user = user.replace('#include "../../../../../../../experiments/ticket03/blade_gear_presets.h"',
                        '#include "'+str(root/'experiments/ticket03/blade_gear_presets.h')+'"')
    user = user.replace('#include "../../thirdparty/mini_print/mini_vsnprintf.h"', '#include "'+str(root/'OpenI6X/FS-I6X/opentx/radio/src/thirdparty/mini_print/mini_vsnprintf.h')+'"')
    user = user.replace('static coord_t mowerZhText(', 'static coord_t mowerZhDraw(')
    wrapper = r'''
static coord_t mowerZhText(coord_t x,coord_t y,const char *value,bool inverse) {
    const char *cursor=value;
    while (*cursor) {
      const uint16_t code=mowerZhNext(cursor);
      bool found=false;
      for (auto glyph:mowerZhGlyphs) if (glyph.code==code) found=true;
      assert(code<128 ? code>=32 : found);
    }
    labels.emplace_back(value);
    textPositions.push_back({x,y,value});
    unsigned width=0;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;++p)
        if(*p < 128) width+=6;
        else if((*p & 0xC0) != 0x80) width+=11;
    assert(x>=0 && x+width<=128 && y>=0 && y+12<=64);
    return mowerZhDraw(x,y,value,inverse);
}
'''
    chassis_text=(root/'control_chassis/Core/UserDev/remote_backend.c').read_text()
    decoder_start=chassis_text.index('static uint8_t RemoteBackend_DecodeBladeHeight(')
    decoder_end=chassis_text.index('\n#endif', decoder_start)
    decoder = r'''
struct FlySkyIbusConfig_t { uint16_t blade_height_t1_us,blade_height_t2_us,blade_height_t3_us,blade_height_t4_us,blade_height_hysteresis_us; };
#define REMOTE_BLADE_HEIGHT_T1_US 1200
#define REMOTE_BLADE_HEIGHT_T2_US 1400
#define REMOTE_BLADE_HEIGHT_T3_US 1600
#define REMOTE_BLADE_HEIGHT_T4_US 1800
#define REMOTE_BLADE_HEIGHT_HYSTERESIS_US 25
'''+chassis_text[decoder_start:decoder_end]
    route = text[text.index('void menuMainView(event_t event)'):text.index('  uint8_t view = g_eeGeneral.view;', text.index('void menuMainView(event_t event)'))]+'}\n'
    delivery = r'''
static void deliverKey(event_t event) {
    // Same killEvents contract as keys.cpp: BREAK/REPEAT are suppressed until release.
    if ((event & 0xE0)==0x20 && killedKey==unsigned(event & 0x1F)) { killedKey=99; return; }
    menuMainView(event);
}
'''
    cpp.write_text(prefix+'\n'+function('static int32_t mowerSensor(')+'\n'+function('void mowerUiReadLive(')+'\n'+function('static uint16_t mowerTravelSpeedCms(')+'\n'+declarations+'\n'+function('static Exp3UiState mowerExp3Step(')+'\n'+function('void lcdDrawNumber(coord_t x, coord_t y, int val, LcdFlags flags, uint8_t len)', native_lcd)+'\n'+user+'\n'+wrapper+'\n'+route+'\n'+delivery+'\n'+decoder+'\n'+tests)
    # Compile C separately, matching the firmware language boundary.
    objects=[]
    for src in [root/'experiments/ticket03/exp3_probe.c', root/'OpenI6X/FS-I6X/opentx/radio/src/thirdparty/mini_print/mini_vsnprintf.c']:
        obj=work/(src.stem+'.o')
        subprocess.run(['cc','-std=c99','-fsanitize=address,undefined','-DEXP3_ENABLED=1','-DMOWER_BLADE_OK_ENABLED=1','-c',str(src),'-o',str(obj)],check=True)
        objects.append(str(obj))
    for avoidance in (1, 0):
        subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-DPCBI6X=1','-DPCBI6X_MOWER_UI_SHELL=1','-DPCBI6X_MOWER_BLADE_OK=1',
            '-DEXP3_ENABLED=1','-DMOWER_BLADE_OK_ENABLED=1',f'-DMOWER_ULTRASONIC_AVOIDANCE_ENABLED={avoidance}',
            str(cpp),*objects,'-o',str(work/'ui')],check=True)
        env=os.environ.copy()
        if avoidance:
            env.pop('MOWER_UI_PREVIEW_DIR',None)
        subprocess.run([str(work/'ui')],check=True,env=env)

print('PASS Chinese production UI, live provider, actual main-view route, CH9/CH6 boundaries, scrolling and pixel bounds: UNKNOWN inhibits OK; explicit completion, context/token isolation, late matching receipt, runtime home')
