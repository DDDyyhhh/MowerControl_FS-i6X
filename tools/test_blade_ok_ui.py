#!/usr/bin/env python3
"""Execute the actual 128x64 home and overlay renderers against display fakes."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = root / 'OpenI6X/FS-I6X/opentx/radio/src/gui/128x64/view_main.cpp'
text = source.read_text()
def function(signature):
    start = text.index(signature)
    opened = text.index('{', start)
    depth = 1
    end = opened + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
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
enum { SMLSIZE=1, RIGHT=2, INVERS=4, ERASE=8, BLINK=16, PREC2=32, FORCE=64 };
enum { VIEW_MOWER=0, KEY_ENTER=0, KEY_MENU=0, KEY_EXIT=1, KEY_RIGHT=4, KEY_LEFT=5, KEY_UP=3, KEY_DOWN=2 };
#define EVT_KEY_BREAK(x) (0x20|(x))
#define EVT_KEY_LONG(x) (0x80|(x))
#define EVT_KEY_REPT(x) (0x40|(x))
static std::vector<std::string> labels;
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
static unsigned submissions;
void exp3_board_status(Exp3BoardStatus *s) { *s=board; }
static Exp3Message submitted;
bool exp3_board_submit(const Exp3Message *message) { submitted=*message; ++submissions; return true; }
static bool controlKnown=true;
static uint16_t controlStatus=MOWER_CONTROL_ARMED|MOWER_CONTROL_VALID, controlGeneration=1U;
static bool exp3_board_control(uint32_t,uint16_t *generation,uint16_t *status) {
    *generation=controlGeneration; *status=controlStatus; return controlKnown;
}
static bool known;
static uint16_t runtime;
static bool exp3_board_runtime(uint32_t,uint16_t *word) { *word=runtime; return known; }
static bool bagKnown, bagConnected;
static bool exp3_board_bag_connection(uint32_t,bool *connected) {
    *connected=bagConnected; return bagKnown;
}
static bool has(const char *value) {
    for(const auto &label:labels) if(label==value) return true;
    return false;
}
static void resetDraw() { labels.clear(); memset(pixels,0,sizeof(pixels)); }
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
    board.bound=true; board.controller=1; board.hmi=2;
    channelOutputs[3]=0; known=false; mowerExp3Step(0);
    channelOutputs[3]=-1024;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE && submissions==0);
    known=true; runtime=8;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE && submissions==0);
    channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT);
    known=false;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE && submissions==0);
    known=true; channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024; mowerExp3Step(0);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==1);
    board.waiting=true; ticks+=199;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==1);
    ticks+=1;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE && submissions==1);
    assert(mowerExp3Probe.state==EXP3_UI_WAIT); // background still awaits matching result
    channelOutputs[3]=0;
    assert(mowerExp3Step(0)==EXP3_UI_CANCEL_WAIT);
    ticks+=200;
    assert(mowerExp3Step(0)==EXP3_UI_IDLE);
    assert(mowerExp3Probe.state==EXP3_UI_CANCEL_WAIT);
    board.waiting=false; mowerExp3Step(0);
    assert(submissions==2); // deferred Cancel still sent after overlay disappears
    board.waiting=true; ticks+=1;
    assert(mowerExp3Step(0)==EXP3_UI_IDLE); // no timer restart on retry
    board.waiting=false; mowerExp3Step(0);

    // A new action can finish after its modal has already disappeared.
    channelOutputs[3]=-1024; mowerExp3Step(0);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==3);
    ticks+=200; assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_IDLE && submissions==3);
    board.matches=1; board.last_result=mowerExp3Probe.pending;
    board.last_result.kind=EXP3_RESULT; board.last_result.command=EXP3_CONFIRM;
    board.last_result.status=EXP3_ACCEPTED;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_ACCEPTED && submissions==3);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_ACCEPTED && submissions==3);
    known=true; runtime=15;
    mowerExp3DisplayState=EXP3_UI_ACCEPTED;
    renderFixture=true; fixture.telemetry_online=true;
    fixture.valid=MOWER_UI_SOC|MOWER_UI_FAULT;
    fixture.soc_percent=82; fixture.radio_battery_100mv=78;
    fixture.radio_quality=100; fixture.height_setting=3; fixture.bag_command=2;
    labels.clear(); mowerUserRender();
    assert(has("已连接") && has("高") && has("开") && has("1.10m/s"));
    fixture.valid |= MOWER_UI_BAG_CONNECTION;
    for(unsigned command=0; command<=2; ++command) {
      fixture.bag_command=command; fixture.bag_connected=false;
      resetDraw(); mowerUserRender(); assert(has("草袋未连接"));
      assert(!has("袋指令") && !has("草袋开") && !has("草袋关"));
    }
    fixture.bag_connected=true;
    const char *bagLabels[]={"草袋--","草袋开","草袋关"};
    for(unsigned command=0; command<=2; ++command) {
      fixture.bag_command=command;
      resetDraw(); mowerUserRender(); assert(has(bagLabels[command]));
    }
    fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); assert(has("草袋未知"));
    fixture.telemetry_online=false; fixture.valid |= MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); assert(has("草袋未知"));
    fixture.telemetry_online=true; fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    fixture.bag_command=2;
    channelOutputs[9]=-1000; mowerUserRender(); assert(has("0.40m/s"));
    channelOutputs[9]=1000; mowerUserRender(); assert(has("1.80m/s"));
    channelOutputs[9]=-500; mowerUserRender(); assert(has("0.75m/s"));
    channelOutputs[9]=500; mowerUserRender(); assert(has("1.45m/s"));
    channelOutputs[9]=0;
    labels.clear(); runtime=8; mowerUserRender();
    assert(has("关") && !has("开"));
    labels.clear(); known=false; mowerUserRender();
    assert(has("--") && !has("开") && !has("高"));
    labels.clear(); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("确认启动割刀") && has("按OK确认"));
    labels.clear(); mowerExp3Overlay(EXP3_UI_REJECTED);
    assert(has("启动被拒绝") && !has("开"));

    // Cancel a start and re-enter CH4 immediately while the old wire command
    // is still busy. The new prompt must be visible without authorizing output.
    mowerExp3Initialized=false; board.waiting=false; known=true; submissions=0;
    channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==1);
    board.waiting=true; ticks+=1; channelOutputs[3]=0;
    assert(mowerExp3Step(0)==EXP3_UI_CANCEL_WAIT);
    ticks+=1; channelOutputs[3]=-1024;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==1);
    assert(mowerExp3Probe.cancel_pending && mowerExp3Probe.state==EXP3_UI_CANCEL_WAIT);
    const uint32_t generation=mowerExp3Probe.prompt_generation;
    const uint32_t oldAction=mowerExp3Probe.pending.action;
    for (unsigned i=0; i<10; ++i) {
        ticks+=1;
        assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==1);
        assert(mowerExp3Probe.prompt_generation==generation);
        assert(mowerExp3Probe.pending.action==oldAction);
    }
    // A delayed old acceptance must not authorize the fresh pending prompt.
    board.last_result=mowerExp3Probe.pending;
    board.last_result.kind=EXP3_RESULT; board.last_result.command=EXP3_CONFIRM;
    board.last_result.status=EXP3_ACCEPTED; ++board.matches;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==1);
    labels.clear(); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("等待停刀") && !has("按OK确认"));
    ticks+=201;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==1);
    // Old Confirm has completed; Cancel must still be sent before new Confirm.
    board.waiting=false;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==2);
    board.waiting=true; ticks+=1;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==2);
    board.waiting=false; ticks+=1;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT && submissions==2);
    labels.clear(); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("按OK确认") && !has("等待停刀"));
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==3);
    // Stale runtime hides the provisional prompt and discards its start edge.
    board.waiting=true; channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024; known=false;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))!=EXP3_UI_PROMPT && submissions==3);
    known=true; board.waiting=false;
    mowerExp3Step(0); // sends only pending Cancel
    assert(submissions==4);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))!=EXP3_UI_PROMPT && submissions==4);
    channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024;
    assert(mowerExp3Step(0)==EXP3_UI_PROMPT && submissions==4);

    mowerExp3Initialized=false; board.waiting=false; known=true; submissions=0;
    channelOutputs[3]=0; mowerExp3Step(0);
    channelOutputs[3]=-1024; mowerExp3Step(0);
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT && submissions==1);
    board.waiting=true; channelOutputs[3]=0; ticks+=1;
    mowerExp3Step(0);
    board.cancel_ready=true; // frame sent, lost Confirm response still waiting
    channelOutputs[3]=-1024; ticks+=1;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==2);
    board.cancel_ready=false; ticks+=1001;
    assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_PROMPT && submissions==2);
    labels.clear(); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("停刀无回执") && !has("按OK确认"));

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
    MowerUiSnapshot live;
    channelOutputs[8]=-1000; channelOutputs[5]=1000;
    mowerUiReadLive(live);
    assert(live.telemetry_online && live.soc_percent==82 && live.height_setting==1 && live.bag_command==2);
    assert(!(live.valid & MOWER_UI_BAG_CONNECTION)); // Old chassis: unknown.
    bagKnown=true; bagConnected=false;
    const int bagOutputs[]={-1000,1000,0,-1000,1000};
    for(auto output:bagOutputs) {
      channelOutputs[5]=output;
      mowerUiReadLive(live);
      assert((live.valid & MOWER_UI_BAG_CONNECTION) && !live.bag_connected);
      renderFixture=true; fixture=live;
      resetDraw(); mowerUserRender(); assert(has("草袋未连接"));
    }
    bagConnected=true;
    for(auto output:bagOutputs) {
      channelOutputs[5]=output; mowerUiReadLive(live); fixture=live;
      assert((live.valid & MOWER_UI_BAG_CONNECTION) && live.bag_connected);
      resetDraw(); mowerUserRender();
      assert(has(output < 0 ? "草袋开" : output > 0 ? "草袋关" : "草袋--"));
    }
    bagKnown=false; mowerUiReadLive(live); fixture=live;
    resetDraw(); mowerUserRender(); assert(has("草袋未知"));
    renderFixture=false;
    channelCenters[8]=1550; channelOutputs[8]=900; // transmitted pulse=2000
    channelCenters[5]=1550; channelOutputs[5]=-1100; // transmitted pulse=1000
    mowerUiReadLive(live); assert(live.height_setting==5 && live.bag_command==1);
    channelCenters[8]=1500; channelCenters[5]=1500;
    channelOutputs[8]=1000; channelOutputs[5]=-1000; // model reversal is already in channelOutputs
    mowerUiReadLive(live); assert(live.height_setting==5 && live.bag_command==1);
    channelCenters[9]=1600; channelOutputs[9]=-1200; assert(mowerTravelSpeedCms()==40);
    channelOutputs[9]=800; assert(mowerTravelSpeedCms()==180);
    channelCenters[9]=1500; channelOutputs[9]=0;
    channelOutputs[8]=2000; channelOutputs[5]=0;
    mowerUiReadLive(live); assert(live.height_setting==0 && live.bag_command==0);
    // Receiver RSSI alone cannot claim chassis connection.
    for(unsigned i=4;i<7;++i) telemetryItems[i].old=true;
    mowerUiReadLive(live); assert(!live.telemetry_online);
    telemetryItems[4].old=false;
    mowerUiReadLive(live); assert(live.telemetry_online && !(live.valid & MOWER_UI_FAULT));
    assert(std::string(mowerConnection(live))=="状态未知");
    assert(!(live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)));
    telemetryItems[6].old=false; telemetryItems[6].value=0x1C0;
    mowerUiReadLive(live);
    assert(!(live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)));
    telemetryItems[6].value=0; mowerUiReadLive(live);
    assert((live.valid & (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM)) ==
        (MOWER_UI_SOC|MOWER_UI_VOLTAGE|MOWER_UI_WALK_ACTUAL|MOWER_UI_BLADE_RPM));
    telemetryItems[6].value=0x30F;
    mowerUiReadLive(live); assert(std::string(mowerConnection(live))=="报错");
    g_model.telemetrySensors[7].id=0xE0; telemetryItems[7].available=true;
    mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_FAULT));
    telemetryItems[7].available=false;
    telemetryItems[1].value=101; mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_SOC));
    streaming=false; mowerUiReadLive(live);
    assert(!live.valid && !live.telemetry_online && live.radio_battery_100mv==78);
    bagKnown=true; mowerUiReadLive(live); assert(!(live.valid & MOWER_UI_BAG_CONNECTION));
    bagKnown=false;

    // Exercise the production main-view router with real probe state.
    renderFixture=true; fixture.valid=MOWER_UI_SOC|MOWER_UI_FAULT;
    fixture.telemetry_online=true; fixture.fault_bits=0;
    board={}; board.bound=true; board.controller=1; board.hmi=2;
    mowerExp3Initialized=false; known=true; runtime=8; submissions=0;
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
    // Menu-selection OK cannot confirm the CH4 edge arriving on this frame.
    menuMainView(EVT_KEY_BREAK(KEY_ENTER));
    assert(submissions==0 && mowerUserPage==MOWER_USER_HOME && mowerExp3DisplayState==EXP3_UI_PROMPT);
    menuMainView(EVT_KEY_LONG(KEY_MENU));
    deliverKey(EVT_KEY_BREAK(KEY_MENU)); assert(submissions==0);
    menuMainView(EVT_KEY_LONG(KEY_RIGHT)); assert(mowerUserPage==MOWER_USER_HOME);
    deliverKey(EVT_KEY_BREAK(KEY_RIGHT)); assert(submissions==0);
    menuMainView(EVT_KEY_BREAK(KEY_EXIT)); assert(submissions==0 && mowerExp3DisplayState==EXP3_UI_PROMPT);
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(submissions==1);
    board.waiting=true;
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(submissions==1);
    ticks+=201;
    menuMainView(EVT_KEY_LONG(KEY_RIGHT)); assert(mowerUserPage==MOWER_USER_MAINTENANCE);
    channelOutputs[3]=0; menuMainView(0);
    assert(mowerExp3Probe.cancel_pending && mowerUserPage==MOWER_USER_HOME);
    ticks+=201; menuMainView(EVT_KEY_LONG(KEY_RIGHT));
    assert(mowerUserPage==MOWER_USER_MAINTENANCE);
    board.waiting=false; menuMainView(0); assert(submissions==2); // background cancel drains
    menuMainView(EVT_KEY_BREAK(KEY_ENTER)); assert(submissions==2);

    // Pixel snapshots use the actual checked-in glyphs and production drawers.
    mowerExp3DisplayState=EXP3_UI_IDLE; mowerExp3Probe.state=EXP3_UI_IDLE;
    mowerExp3Probe.cancel_pending=false; mowerExp3CancelFailed=false;
    mowerUserPage=MOWER_USER_HOME; runtime=15; fixture.height_setting=3; fixture.bag_command=2;
    resetDraw(); mowerUserRender(); saveScreen("home-online");
    fixture.valid |= MOWER_UI_BAG_CONNECTION; fixture.bag_connected=false;
    resetDraw(); mowerUserRender(); saveScreen("bag-disconnected"); assert(has("草袋未连接"));
    fixture.bag_connected=true; fixture.bag_command=1;
    resetDraw(); mowerUserRender(); saveScreen("bag-open"); assert(has("草袋开"));
    fixture.bag_command=2;
    resetDraw(); mowerUserRender(); saveScreen("bag-closed"); assert(has("草袋关"));
    fixture.bag_command=0;
    resetDraw(); mowerUserRender(); saveScreen("bag-command-unknown"); assert(has("草袋--"));
    fixture.bag_command=2; fixture.valid &= ~MOWER_UI_BAG_CONNECTION;
    resetDraw(); mowerUserRender(); saveScreen("bag-unknown"); assert(has("草袋未知"));
    fixture.telemetry_online=false; fixture.valid=0;
    resetDraw(); mowerUserRender(); saveScreen("home-offline");
    assert(has("未连接") && has("--") && !has("100") && !has("高") && !has("开"));
    fixture.telemetry_online=true; fixture.valid=MOWER_UI_FAULT|MOWER_UI_SOC; fixture.fault_bits=0x30F;
    resetDraw(); mowerUserRender(); saveScreen("home-fault"); assert(has("报错"));
    mowerUserPage=MOWER_USER_MAINTENANCE;
    resetDraw(); mowerUserRender(); saveScreen("maintenance");
    mowerUserPage=MOWER_USER_DEBUG;
    for(unsigned first=0;first<MOWER_DEBUG_ROWS;first+=4) {
      mowerDebugFirstRow=first > MOWER_DEBUG_ROWS-4 ? MOWER_DEBUG_ROWS-4 : first;
      resetDraw(); mowerUserRender();
      std::string name="debug-"+std::to_string(first); saveScreen(name.c_str());
    }
    assert(has("割刀反馈过期") && has("割刀校验异常") && has("!"));
    mowerUserPage=MOWER_USER_HOME; mowerExp3DisplayState=EXP3_UI_PROMPT;
    resetDraw(); mowerUserRender(); saveScreen("confirm");
    mowerExp3Probe.cancel_pending=true; mowerExp3Probe.state=EXP3_UI_CANCEL_WAIT;
    mowerExp3WaitStarted=ticks*10;
    resetDraw(); mowerUserRender(); saveScreen("stopping");
    ticks+=1001; resetDraw(); mowerUserRender(); saveScreen("stop-no-reply");
    for(auto state:{EXP3_UI_WAIT,EXP3_UI_CANCEL_WAIT,EXP3_UI_TIMEOUT,EXP3_UI_UNBOUND,EXP3_UI_REJECTED}) {
      resetDraw(); mowerExp3Overlay(state);
    }

    // Independent chassis OK, followed by a fresh physical blade request.
    ++board.hmi; board.waiting=false; board.bound=true;
    controlKnown=true; controlGeneration=7U; controlStatus=MOWER_CONTROL_VALID|MOWER_CONTROL_READY;
    known=true; runtime=8U; channelOutputs[3]=-1024;
    unsigned startSubmissions=submissions;
    assert(mowerControlStep(0)==EXP3_UI_PROMPT && mowerChassisOverlay);
    fixture.radio_quality=90; fixture.soc_percent=82; fixture.height_setting=3;
    fixture.telemetry_online=true; fixture.valid=MOWER_UI_SOC|MOWER_UI_FAULT|MOWER_UI_BAG_CONNECTION;
    fixture.bag_connected=true; fixture.bag_command=2;
    resetDraw(); mowerUserHome(fixture,true,8U); mowerExp3Overlay(EXP3_UI_PROMPT);
    assert(has("底盘解锁") && !has("确认启动割刀")); saveScreen("chassis-unlock");
    assert(mowerControlStep(EVT_KEY_BREAK(KEY_ENTER))==EXP3_UI_WAIT);
    assert(submissions==startSubmissions+1 && submitted.context==MOWER_CHASSIS_CONTEXT && submitted.generation==7U);
    uint32_t chassisAction=submitted.action;
    board.waiting=true;
    mowerControlStep(EVT_KEY_BREAK(KEY_ENTER)); assert(submissions==startSubmissions+1);
    controlStatus=MOWER_CONTROL_VALID|MOWER_CONTROL_ARMED;
    board.last_result=mowerChassisProbe.pending; board.last_result.kind=EXP3_RESULT;
    board.last_result.command=EXP3_CONFIRM; board.last_result.status=EXP3_ACCEPTED;
    ++board.matches; board.waiting=false;
    mowerControlStep(0); mowerControlStep(0);
    assert(submissions==startSubmissions+1 && !mowerChassisOverlay);
    assert(mowerExp3Probe.state==EXP3_UI_IDLE); // held CH4 cannot reuse chassis OK
    channelOutputs[3]=0; mowerControlStep(0);
    channelOutputs[3]=-1024; assert(mowerControlStep(0)==EXP3_UI_PROMPT && !mowerChassisOverlay);
    mowerControlStep(EVT_KEY_BREAK(KEY_ENTER));
    assert(submitted.context==EXP3_PWM_CONTEXT && submitted.action>chassisAction);
    controlStatus=MOWER_CONTROL_VALID|MOWER_CONTROL_ARMED|MOWER_CONTROL_API_SELECTED;
    mowerControlStep(0);
    resetDraw(); mowerUserHome(fixture,true,8U); assert(has("API") && has("解锁")); saveScreen("api-mode");
    controlKnown=false; mowerControlStep(EVT_KEY_BREAK(KEY_ENTER));
    resetDraw(); mowerUserHome(fixture,true,8U); assert(has("模式未知"));
    // A retired/rejected unlock can show a new token without a physical CH4 edge.
    board.waiting=false; mowerExp3Probe.state=EXP3_UI_IDLE;
    controlKnown=true; controlStatus=MOWER_CONTROL_VALID|MOWER_CONTROL_READY;
    mowerChassisProbe.state=EXP3_UI_REJECTED;
    mowerChassisProbe.pending.action=123U; mowerChassisProbe.pending.generation=7U;
    controlGeneration=8U; mowerControlStep(0);
    assert(mowerChassisProbe.state==EXP3_UI_PROMPT);
    // Outstanding blade cancellation holds the new chassis OK until it drains.
    mowerExp3Probe.state=EXP3_UI_CANCEL_WAIT; board.waiting=true;
    startSubmissions=submissions; mowerControlStep(EVT_KEY_BREAK(KEY_ENTER));
    assert(submissions==startSubmissions);


}
'''
with tempfile.TemporaryDirectory(prefix='blade-ok-ui-') as work:
    work = Path(work)
    cpp = work/'ui.cpp'
    declarations = text[text.index('static Exp3UiProbe mowerExp3Probe;'):text.index('static Exp3UiState mowerExp3Step(')]
    user = (source.parent/'mower_user_ui.h').read_text()
    user = user.replace('#pragma once', '').replace('#include "mower_zh_font.h"', '#include "'+str(source.parent/'mower_zh_font.h')+'"')
    user = user.replace('#include "../../thirdparty/mini_print/mini_vsnprintf.h"', '#include "'+str(root/'OpenI6X/FS-I6X/opentx/radio/src/thirdparty/mini_print/mini_vsnprintf.h')+'"')
    user = user.replace('static coord_t mowerZhText(', 'static coord_t mowerZhDraw(')
    wrapper = r'''
static coord_t mowerZhText(coord_t x,coord_t y,const char *value,bool inverse) {
    labels.emplace_back(value);
    unsigned width=0;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;++p)
        if(*p < 128) width+=6;
        else if((*p & 0xC0) != 0x80) width+=12;
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
    cpp.write_text(prefix+'\n'+function('static bool mowerSensor(')+'\n'+function('void mowerUiReadLive(')+'\n'+function('static uint16_t mowerTravelSpeedCms(')+'\n'+declarations+'\n'+function('static Exp3UiState mowerExp3Step(')+'\n'+function('static Exp3UiState mowerControlStep(')+'\n'+function('static void mowerExp3Overlay(')+'\n'+user+'\n'+wrapper+'\n'+route+'\n'+delivery+'\n'+decoder+'\n'+tests)
    # Compile C separately, matching the firmware language boundary.
    objects=[]
    for src in [root/'experiments/ticket03/exp3_probe.c', root/'OpenI6X/FS-I6X/opentx/radio/src/thirdparty/mini_print/mini_vsnprintf.c']:
        obj=work/(src.stem+'.o')
        subprocess.run(['cc','-std=c99','-fsanitize=address,undefined','-DEXP3_ENABLED=1','-DMOWER_BLADE_OK_ENABLED=1','-c',str(src),'-o',str(obj)],check=True)
        objects.append(str(obj))
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-DPCBI6X=1','-DPCBI6X_MOWER_UI_SHELL=1','-DPCBI6X_MOWER_BLADE_OK=1',
        '-DEXP3_ENABLED=1','-DMOWER_BLADE_OK_ENABLED=1',str(cpp),*objects,'-o',str(work/'ui')],check=True)
    subprocess.run([str(work/'ui')],check=True)
print('PASS Chinese production UI, live provider, actual main-view route, CH9/CH6 boundaries, scrolling and pixel bounds: UNKNOWN inhibits OK/prompt; 2s modal limit, deferred Cancel, late matching receipt, no repeat authorization; independent chassis OK, shared actions, retired tokens and API home')
