#!/usr/bin/env python3
"""Execute actual startup, throttle and switch checks for mower/native radios."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'OpenI6X/FS-I6X/opentx/radio/src'
text = (src / 'opentx.cpp').read_text()


def function(signature, text=text):
    start = text.index(signature)
    opened = text.index('{', start)
    end, depth = opened + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define GUI
#define TRACE(...) ((void)0)
#define OPENTX_START_DEFAULT_ARGS 0
#define OPENTX_START_NO_CALIBRATION 1
#define OPENTX_START_NO_SPLASH 2
#define OPENTX_START_NO_CHECKS 4
#define VIEW_MOWER 0
#define NUM_STICKS 4
#define NUM_POTS 2
#define NUM_SLIDERS 0
#define CALIBRATED_POT1 4
#define CALIBRATED_POT2 5
#define THR_STICK 0
#define THRCHK_DEADBAND 16
#define e_perout_mode_notrainer 0
#define AU_THROTTLE_ALERT 1
#define STR_THROTTLEWARN "THROTTLE WARNING"
#define STR_THROTTLENOTIDLE "Throttle not idle"
#define STR_PRESSANYKEYTOSKIP "Press any key to skip"
#define LED_ERROR_BEGIN() ((void)0)
#define LED_ERROR_END() ((void)0)
#define START_SILENCE_PERIOD() ((void)0)
#define PLAY_MODEL_NAME() ((void)0)
#define GET_ADC_IF_MIXER_NOT_RUNNING() ((void)0)
#define WDG_RESET() ((void)0)
#define RTOS_WAIT_MS(ms) ((void)0)
#define RAISE_ALERT(...) (++alerts)
#define NUM_SWITCHES 4
#define SWITCH_WARNING_ALLOWED(i) true
#define SWITCH_WARNING_LIST_X 0
#define SWITCH_WARNING_LIST_Y 0
#define SWITCH_WARNING_LIST_INTERVAL 10
#define MIXSRC_FIRST_SWITCH 0
#define INVERS 1
#define FW 6
#define FH 8
#define AU_SWITCH_ALERT 2
#define AU_NONE 0
#define STR_SWITCHWARN "SWITCH WARNING"
#define AUDIO_ERROR_MESSAGE(...) ((void)0)
#define UNUSED(x) ((void)(x))
using swarnstate_t=uint16_t;
using LcdFlags=unsigned;
static swarnstate_t switches_states, physicalSwitches;
static unsigned switchAlerts, keyDownPolls, movedSwitchPolls;
static int lcdNextPos;
static void getMovedSwitch() { switches_states=physicalSwitches; ++movedSwitchPolls; }
static void resetBacklightTimeout() {}
static void drawAlertBox(const char *, const char *, const char *) { ++switchAlerts; }
static void drawSource(int, int, int, LcdFlags) {}
static void lcdDrawChar(int, int, char, LcdFlags) {}
static void drawSwitch(int, int, int, LcdFlags) {}
static void lcdRefresh() {}
static void lcdSetContrast() {}
static bool keyDown() { ++keyDownPolls; return true; }
struct CalibData { int16_t mid, spanNeg, spanPos; };
static struct { CalibData calib[6]; uint16_t chkSum; uint8_t view; } g_eeGeneral;
static struct {
    bool disableThrottleWarning, throttleReversed;
    uint8_t thrTraceSrc;
    swarnstate_t switchWarningState;
    uint8_t switchWarningEnable;
} g_model;
static int16_t calibratedAnalogs[6], channelOutputs[14];
static unsigned alerts, keyPolls, failsafeChecked, alarmsChecked, calibrationMenus;
static const unsigned menuFirstCalib=1;
static uint16_t evalChkSum() { return 1234; }
static void evalInputs(int) {}
static void checkFailsafe() { ++failsafeChecked; }
static void checkAlarm() { ++alarmsChecked; }
static bool waitKeysReleased() { return true; }
static void showMessageBox(const char *) {}
#define STR_KEYSTUCK "Key stuck"
using tmr10ms_t=unsigned;
static tmr10ms_t get_tmr10ms() { return 0; }
static void doSplash() {}
static void chainMenu(unsigned menu) { assert(menu==menuFirstCalib); ++calibrationMenus; }
static unsigned getEvent() { ++keyPolls; return 1; }
static unsigned pwrCheck() { return 1; }
#define e_power_off 0
static void checkBacklight() {}
'''
code = prefix + '\n#include "i6x_calibration.h"\n'
switches = (src / 'switches.cpp').read_text()
switch_start = switches.index('void checkSwitches()')
code += switches[switch_start:switches.index('\n#endif // GUI', switch_start)] + '\n'
for signature in ('bool isThrottleWarningAlertNeeded()', 'void checkThrottleStick()',
                  'void checkAll()', 'void opentxStart('):
    code += '\n' + function(signature) + '\n'
code += r'''
static void reset(int throttle) {
    for(auto &calib:g_eeGeneral.calib) calib={1024,900,900};
    g_eeGeneral.chkSum=evalChkSum(); g_eeGeneral.view=9;
    g_model={};
    for(auto &analog:calibratedAnalogs) analog=throttle;
    for(unsigned i=0;i<14;++i) channelOutputs[i]=int16_t(i*37-200);
    alerts=keyPolls=failsafeChecked=alarmsChecked=calibrationMenus=0;
    switchAlerts=keyDownPolls=movedSwitchPolls=0;
    switches_states=physicalSwitches=0;
    lcdNextPos=0;
}
static swarnstate_t switchPositions(unsigned combination) {
    swarnstate_t positions=0;
    for(unsigned i=0;i<NUM_SWITCHES;++i) {
        positions |= (combination%3) << (2*i);
        combination /= 3;
    }
    return positions;
}
int main() {
    // All stick positions, both source directions and persisted warning options.
    for(int throttle:{0,-1024,-1008,-1007,1024}) {
      for(unsigned source=0;source<=3;++source)
      for(bool reverse:{false,true})
      for(bool disabled:{false,true}) {
        reset(throttle); g_model.thrTraceSrc=source;
        g_model.throttleReversed=reverse; g_model.disableThrottleWarning=disabled;
        const int value=(source && reverse) ? -throttle : throttle;
        const bool legacyWarning=!disabled && value>16-1024;
        const auto model=g_model;
        int16_t outputs[14]; memcpy(outputs,channelOutputs,sizeof(outputs));
        opentxStart();
        const unsigned expected=EXPECT_DIRECT ? 0U : unsigned(legacyWarning);
        if(alerts!=expected)
          std::fprintf(stderr,"startup throttle=%d source=%u: alerts=%u, expected=%u\n",
                       throttle,source,alerts,expected);
        assert(alerts==expected && keyPolls==expected);
        assert(!switchAlerts && !keyDownPolls);
        assert(failsafeChecked==1 && alarmsChecked==1);
        assert(!calibrationMenus);
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
        assert(g_eeGeneral.view==VIEW_MOWER);
#else
        assert(g_eeGeneral.view==9);
#endif
        assert(!memcmp(&g_model,&model,sizeof(model)));
        assert(!memcmp(outputs,channelOutputs,sizeof(outputs)));
      }
    }
    // Every valid combination of SA/SB/SC/SD, persisted target and enable mask.
    for(unsigned actual=0;actual<81;++actual)
    for(unsigned target=0;target<81;++target)
    for(unsigned disabled=0;disabled<16;++disabled) {
        reset(-1024); physicalSwitches=switchPositions(actual);
        g_model.switchWarningState=switchPositions(target);
        g_model.switchWarningEnable=disabled;
        const auto model=g_model;
        int16_t outputs[14]; memcpy(outputs,channelOutputs,sizeof(outputs));
        bool mismatch=false;
        for(unsigned i=0;i<NUM_SWITCHES;++i) {
#if !defined(PCBI6X)
            // Legacy non-i6X encoding uses one 3-position switch, then bits.
            if(i==NUM_SWITCHES-1) continue;
            const unsigned mask=i==0 ? 3U : (1U << (i+1));
#else
            const unsigned mask=3U << (2*i);
#endif
            if(!(disabled & (1U<<i)) &&
               ((physicalSwitches & mask)!=(g_model.switchWarningState & mask)))
                mismatch=true;
        }
        opentxStart();
        const unsigned expected=EXPECT_DIRECT ? 0U : unsigned(mismatch);
#if defined(PCBI6X)
        const unsigned drawn=switchAlerts;
#else
        const unsigned drawn=alerts;
#endif
        if(drawn!=expected)
            std::fprintf(stderr,"SWITCH WARNING actual=%u target=%u disabled=%u: alerts=%u expected=%u\n",
                         actual,target,disabled,drawn,expected);
        assert(drawn==expected && keyDownPolls==expected);
        assert(!keyPolls && failsafeChecked==1 && alarmsChecked==1 && !calibrationMenus);
        assert(!memcmp(&g_model,&model,sizeof(model)));
        assert(!memcmp(outputs,channelOutputs,sizeof(outputs)));
        assert(physicalSwitches==switchPositions(actual));
    }
    reset(0); ++g_eeGeneral.chkSum; opentxStart();
    assert(calibrationMenus==1 && !alerts && !movedSwitchPolls);
#if defined(PCBI6X)
    for(unsigned i=0;i<NUM_STICKS;++i) {
      reset(0); g_eeGeneral.calib[i].spanNeg=0; opentxStart();
      assert(calibrationMenus==1 && !alerts && !movedSwitchPolls);
    }
#endif
}
'''

with tempfile.TemporaryDirectory(prefix='i6x-startup-warning-') as work:
    work = Path(work)
    cpp = work / 'startup.cpp'
    cpp.write_text('#include <initializer_list>\n' + code)
    for name, flags in (
        ('mower', ['-DPCBI6X=1', '-DPCBI6X_MOWER_UI_SHELL=1', '-DEXPECT_DIRECT=1']),
        ('native-i6x', ['-DPCBI6X=1', '-DEXPECT_DIRECT=0']),
        ('other-board', ['-DPCBI6X_MOWER_UI_SHELL=1', '-DEXPECT_DIRECT=0']),
    ):
        binary = work / name
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                        '-I', str(src), *flags, str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f'PASS {name}: actual startup/throttle/switch paths, 104976 switch cases, calibration/failsafe retained', flush=True)
