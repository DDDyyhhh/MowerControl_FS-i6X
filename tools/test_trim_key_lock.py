#!/usr/bin/env python3
"""Actual FlySky key scanner/debouncer/trim handler; retain saved trim outputs."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
radio = root / 'OpenI6X/FS-I6X/opentx/radio/src'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


driver = (radio / 'targets/flysky/keys_driver.cpp').read_text()
key_source = (radio / 'keys.cpp').read_text()
app = (radio / 'opentx.cpp').read_text()
mixer = (radio / 'mixer.cpp').read_text()
rf = (radio / 'targets/flysky/AFHDS2A_a7105.cpp').read_text()
board = (radio / 'targets/flysky/board.h').read_text()
keys_enum = board[board.index('enum EnumKeys'):board.index('enum EnumSwitches')]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
using std::min;
using std::max;
struct InactivityData { unsigned counter; };
'''+keys_enum+'\n#include "'+str(radio / 'keys.h')+'"\n'+r'''
#define NUM_TRIMS 4
#define MAX_FLIGHT_MODES 3
#define TRIM_MODE_NONE 31
#define TRIM_EXTENDED_MIN -512
#define TRIM_EXTENDED_MAX 512
#define TRIM_MIN -128
#define TRIM_MAX 128
#define THR_STICK 2
#define RESX 1024
#define RESX_SHIFT 10
#define CONVERT_MODE_TRIMS(x) (x)
#define TRACE(...) ((void)0)
#define IS_SHIFT_KEY(index) false
#define DIM(x) (sizeof(x)/sizeof((x)[0]))
#define AUDIO_TRIM_MIDDLE() ((void)0)
#define AUDIO_TRIM_MIN() ((void)0)
#define AUDIO_TRIM_MAX() ((void)0)
#define AUDIO_TRIM_PRESS(x) ((void)0)
#define EE_MODEL 1
#define PPM_CH_CENTER(ch) centers[ch]
template<class T> T limit(T lo,T v,T hi){return std::max(lo,std::min(v,hi));}
struct trim_t { int value; unsigned mode; };
struct FlightModeData { trim_t trim[4]; };
static struct { FlightModeData flight[3]; int trimInc; bool thrTrim,throttleReversed,extendedTrims;
 uint32_t receiverId; int calibration[12]; } g_model;
static struct { unsigned backlightMode; int calibration[12]; uint32_t receiverId; } g_eeGeneral;
static FlightModeData *flightModeAddress(unsigned phase){return &g_model.flight[phase];}
static uint8_t getTrimFlightMode(uint8_t phase,uint8_t){return phase;}
static unsigned dirty,backlights;
static void storageDirty(unsigned){++dirty;}
static void resetBacklightTimeout(){++backlights;}
static uint8_t mixerCurrentFlightMode;
static int16_t trims[4];
static unsigned trimsCheckTimer,trimsDisplayTimer,trimsDisplayMask;
static int centers[14];
enum { e_backlight_mode_keys=1 };
static uint32_t physical_trims,physical_nav;
struct FakeGPIO { uint32_t IDR; } bind_gpio;
#define KEYS_BIND_GPIO (&bind_gpio)
#define KEYS_BIND_PIN 1U
static uint32_t scanMatrix(uint32_t start,uint32_t end){
 if(start==0&&end==1)return physical_trims;
 assert(start==2&&end==2);return physical_nav;
}
'''
production = key_source[key_source.index('#define KEY_LONG_DELAY'):key_source.index('bool waitKeysReleased()')]
production += '\n' + '\n'.join(function(driver, s) for s in [
    'uint32_t readKeys()', 'uint32_t readTrims()', 'uint8_t trimDown(',
    'bool keyDown()', 'void readKeysAndTrims()'])
production += '\n' + '\n'.join(function(app, s) for s in [
    'trim_t getRawTrimValue(', 'int getTrimValue(', 'bool setTrimValue(',
    'void checkTrims()', 'void evalTrims()'])
production += '\n' + function(mixer, 'int getStickTrimValue(')
production += '\n' + function(rf, 'void setChannelValue(')
test = r'''
static unsigned events[4];
static void sample(bool handle=true){
 readKeysAndTrims();
 event_t e=s_evt;
 if(e){
  unsigned kind=IS_KEY_FIRST(e)?0:IS_KEY_LONG(e)?1:IS_KEY_REPT(e)?2:3;
  ++events[kind];
 }
 if(handle)checkTrims();else (void)getEvent(true);
 (void)getEvent(false);
}
static void clear_keys(){memset(keys,0,sizeof(keys));s_evt=0;memset(events,0,sizeof(events));physical_nav=physical_trims=0;bind_gpio.IDR=1;}
static void packet(uint8_t *p){
 memset(p,0,38);evalTrims();
 for(unsigned ch=0;ch<14;++ch){
  int stick=ch<4?int(ch)*80-120:0;
  int output=stick+(ch<4?getStickTrimValue(ch,stick):0);
  setChannelValue(p,ch,output);
 }
}
int main(){
#if defined(PCBI6X_MOWER_BLADE_OK) && !defined(BOOT)
 const bool locked=true;
#else
 const bool locked=false;
#endif
 g_eeGeneral.backlightMode=e_backlight_mode_keys;
 g_eeGeneral.receiverId=0x10203040U;g_model.receiverId=0x50607080U;
 for(unsigned ch=0;ch<14;++ch)centers[ch]=1500;
 for(unsigned mode=0;mode<3;++mode)for(unsigned ch=0;ch<4;++ch){
  g_model.flight[mode].trim[ch].value=int(ch)*29-54;
  g_model.flight[mode].trim[ch].mode=mode==2U?1U:0U; // direct, inherited, additive.
 }
 for(unsigned ch=0;ch<12;++ch){g_model.calibration[ch]=int(ch)*137;g_eeGeneral.calibration[ch]=int(ch)*193;}
 g_model.extendedTrims=true;
 // Test every saved flight mode and both throttle-trim policies. Read-only
 // scanner changes must not reset saved trim, binding or calibration bytes.
 for(unsigned mode=0;mode<3;++mode)for(bool throttle:{false,true}){
  mixerCurrentFlightMode=mode;g_model.thrTrim=throttle;
  for(unsigned key=TRM_BASE;key<=TRM_LAST;++key)for(unsigned duration:{20U,100U,600U}){
   clear_keys();dirty=backlights=0;
   auto saved=g_model;auto general=g_eeGeneral;
   uint8_t before[38],after[38];packet(before);
   physical_trims=1U<<key;
   assert(readTrims()==physical_trims); // Firmware DFU entry keeps raw access.
   assert(bool(trimDown(key-TRM_BASE))==!locked);
   for(unsigned n=0;n<duration;++n)sample();
   physical_trims=0;for(unsigned n=0;n<12;++n)sample();
   packet(after);
   if(locked){
    assert(!events[0]&&!events[1]&&!events[2]&&!events[3]);
    assert(!dirty&&!backlights);
    assert(!memcmp(&saved,&g_model,sizeof(saved)));
    assert(!memcmp(before,after,sizeof(before)));
   }else{
    assert(events[0]&&dirty);
   }
   assert(!memcmp(&general,&g_eeGeneral,sizeof(general)));
   g_model=saved;
  }
 }
 // Exercise every event type without the normal trim handler's deliberate
 // midpoint pauses/endpoint kills obscuring the debouncer's timed sequence.
 for(unsigned key=TRM_BASE;key<=TRM_LAST;++key)for(unsigned duration:{20U,100U,600U}){
  clear_keys();physical_trims=1U<<key;
  for(unsigned n=0;n<duration;++n)sample(false);
  physical_trims=0;for(unsigned n=0;n<12U;++n)sample(false);
  if(locked)assert(!events[0]&&!events[1]&&!events[2]&&!events[3]);
  else {assert(events[0]&&events[3]);if(duration>=100U)assert(events[1]&&events[2]);}
 }
 // Holding all eight trims cannot mask ordinary menu/OK/EXIT/BIND inputs.
 for(unsigned key:{KEY_DOWN,KEY_UP,KEY_ENTER,KEY_EXIT,KEY_RIGHT}){
  clear_keys();physical_trims=locked?((1U<<(TRM_LAST+1U))-1U)&~((1U<<TRM_BASE)-1U):0;
  if(key==KEY_RIGHT)bind_gpio.IDR=0;else physical_nav=1U<<key;
  for(unsigned n=0;n<110U;++n)sample();
  physical_nav=physical_trims=0;bind_gpio.IDR=1;
  for(unsigned n=0;n<12U;++n)sample();
  assert(events[0]&&events[1]&&events[2]&&events[3]);
 }
 // Raw inwards-trim boot chord remains detectable after the upgrade.
 physical_trims=(1U<<TRM_LH_UP)|(1U<<TRM_RH_DWN);
 assert(readTrims()==physical_trims);
 puts(locked ? "PASS mower trim key lock: all 8 directions, short/long/600-tick repeat, 3 flight modes, stored trims/calibration/binding and actual RF pulse bytes unchanged; normal keys and raw DFU chord retained" : "PASS non-mower/BOOT: original trim key events, edits, long/repeat and navigation retained");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='mower-trim-lock-') as directory:
        work = Path(directory)
        cpp = work / 'keys.cpp'
        cpp.write_text(prefix + production + test)
        for flags in [[], ['-DPCBI6X_MOWER_BLADE_OK=1'], ['-DPCBI6X_MOWER_BLADE_OK=1', '-DBOOT=1']]:
            exe = work / 'test'
            subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-implicit-fallthrough',
                            '-fsanitize=address,undefined', '-DEXP3_ENABLED=1',
                            *flags, str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True)


if __name__ == '__main__':
    main()
