#!/usr/bin/env python3
"""Compile the actual i6X startup preamble with fake EEPROM/GUI boundaries."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'OpenI6X/FS-I6X/opentx/radio/src'
s = (src / 'opentx.cpp').read_text()
a = s.index('void opentxStart(')
b = s.index('\n#if defined(GUI)', s.index('uint8_t calibration_needed', a))
preamble = s[a:b].replace('void opentxStart(', 'bool needsCalibration(')
preamble += '\nreturn calibration_needed;\n}\n'
code = r'''
#include <cassert>
#include <cstdint>
#define PCBI6X
#define PCBI6X_MOWER_UI_SHELL
#define VIEW_MOWER 0
#define TRACE(...) ((void)0)
#define OPENTX_START_DEFAULT_ARGS 0
#define OPENTX_START_NO_CALIBRATION 1
#define NUM_STICKS 4
#define CALIBRATED_POT1 4
#define CALIBRATED_POT2 5
struct CalibData { int16_t mid, spanNeg, spanPos; };
struct { CalibData calib[6]; uint16_t chkSum; uint8_t view; } g_eeGeneral;
uint16_t evalChkSum() { return 1234; }
'''
if (src / 'i6x_calibration.h').exists():
    code += '#include "i6x_calibration.h"\n'
code += preamble
mixer = (src / 'mixer.cpp').read_text()
a = mixer.index('    uint8_t ch =', mixer.index('void evalInputs('))
b = mixer.index('\n#if defined(STICK_DEAD_ZONE)', mixer.index('if (v >  RESX)', a))
code += r'''
#include <algorithm>
using std::max;
#define RESX 1024
#define CONVERT_MODE(i) (i)
#define IS_POT_MULTIPOS(i) false
int16_t calibratedAnalogs[6];
int16_t analogValues[6];
int16_t anaIn(int i) { return analogValues[i]; }
void normalize() {
  for (uint32_t i = 0; i < 6; ++i) {
''' + mixer[a:b] + '\ncalibratedAnalogs[ch] = v;\n}\n}\n'

code += r'''
void valid() {
  for (auto & c : g_eeGeneral.calib) c = {1024, 900, 900};
  g_eeGeneral.chkSum = evalChkSum();
  g_eeGeneral.view = 9;
}
int main() {
  valid(); assert(!needsCalibration(0)); assert(g_eeGeneral.view == VIEW_MOWER);
  for (int i=0; i<NUM_STICKS; ++i) {
    valid(); g_eeGeneral.calib[i] = {0, 0, 0};
    assert(needsCalibration(0)); // valid checksum is not valid travel
    valid(); g_eeGeneral.calib[i].spanNeg = 100;
    assert(needsCalibration(0));
    valid(); g_eeGeneral.calib[i].spanPos = 50;
    assert(needsCalibration(0));
    valid(); g_eeGeneral.calib[i].mid = 2047;
    assert(needsCalibration(0));
  }
  valid(); g_eeGeneral.chkSum++; assert(needsCalibration(0));
  assert(!needsCalibration(OPENTX_START_NO_CALIBRATION));
  valid(); assert(!needsCalibration(0));
  for (int i=0; i<6; ++i) analogValues[i] = 1114;
  normalize();
  for (int i=0; i<NUM_STICKS; ++i) assert(calibratedAnalogs[i] == 102);
  for (int i=0; i<NUM_STICKS; ++i) {
    g_eeGeneral.calib[i] = {0, 0, 0}; normalize();
    assert(calibratedAnalogs[i] == 0);
  }
  valid();
  assert(g_eeGeneral.calib[0].spanNeg == 900); // preserve measured calibration
}
'''
with tempfile.TemporaryDirectory(prefix='i6x-calibration-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(src), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
print('PASS: actual i6X startup rejects missing/narrow stick calibration')
