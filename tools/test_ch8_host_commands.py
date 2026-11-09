#!/usr/bin/env python3
"""Run production CAN producer/consumer functions with the real mode machine."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'control_chassis/Core/UserDev/chassis_control.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

cache = source[source.index('static volatile uint32_t host_publication'):source.index('\n#endif', source.index('static volatile uint32_t host_publication'))]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "mower_mode_control.h"
#define HOST_CMD_FRAME_ID 5U
#define HOST_CMD_PAYLOAD_LEN 8U
#define API_TIMEOUT_MS 500U
static uint32_t now;
static uint32_t HAL_GetTick(void) { return now; }
static uint32_t __get_PRIMASK(void) { return 0U; }
static void __disable_irq(void) {}
static void __enable_irq(void) {}
static MowerModeControl_t mode;
static MowerModeControl_t *RemoteBackend_ModeControl(void) { return &mode; }
static uint8_t Ctrl_Mode, host_blade_speed_pct, host_lifter_gear, host_bag_control;
static uint32_t last_host_cmd_tick;
static float host_vx, host_vy, host_omega;
static void advance(unsigned n, uint16_t raw) {
  MowerModeInput_t in={true,true,false,true,true,raw,mode.frame_sequence};
  while(n--) { ++in.frame_sequence; now+=10U; MowerMode_Advance(&mode,&in,now); }
}
'''
test = r'''
int main(void) {
  uint8_t data[8]={1,5,0,3,2,4,1,0xAA};
  MowerMode_Init(&mode); MowerMode_Session(&mode,1,2); advance(25,1000);
  assert(MowerMode_Confirm(&mode,mode.generation)); advance(2,1000);
  chassis_control_parse_host_frame(5,data,8);
  assert(Ctrl_Mode==0 && host_vx==0 && last_host_cmd_tick==0);
  chassis_control_advance_host(); assert(Ctrl_Mode==0 && mode.armed);
  data[0]=2; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==0 && host_vx==0);
  // A queued standby from before the switch must not satisfy the new handshake.
  data[0]=1; chassis_control_parse_host_frame(5,data,8);
  advance(4,2000); chassis_control_discard_host(); chassis_control_advance_host();
  data[0]=2; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==0 && host_vx==0);
  data[0]=1; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==1 && host_vx==0);
  data[0]=2; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==2 && host_vx==0.5f && host_targets_valid);
  uint32_t applied=chassis_control_host_sequence();
  chassis_control_clear_cached_intents(); chassis_control_advance_host();
  assert(host_vx==0 && !host_targets_valid && chassis_control_host_sequence()==applied);
  chassis_control_parse_host_frame(5,data,8); now+=500U; chassis_control_advance_host();
  assert(host_vx==0 && chassis_control_host_sequence()==applied);
  advance(1,2000); assert(!mode.armed);
  data[0]=1; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(!mode.armed && mode.api_mode==0); // AA and standby cannot unlock.
  advance(25,2000); assert(MowerMode_Confirm(&mode,mode.generation)); advance(2,2000);
  data[0]=1; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  data[0]=2; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==2);
  data[0]=0; chassis_control_parse_host_frame(5,data,8); chassis_control_advance_host();
  assert(Ctrl_Mode==0 && !mode.armed);
  uint32_t publication=host_publication;
  data[0]=3; chassis_control_parse_host_frame(5,data,8);
  data[0]=1; chassis_control_parse_host_frame(5,data,7); chassis_control_parse_host_frame(6,data,8);
  assert(host_publication==publication);
  puts("PASS production CAN has no receive-time side effects, CH8 gate, fresh two-step entry, AA isolation and expiry");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-ch8-can-') as directory:
    directory = Path(directory)
    harness = directory / 'can.c'
    harness.write_text(fixture + cache + '\n' + '\n'.join(function(signature) for signature in [
        'void chassis_control_parse_host_frame(', 'void chassis_control_advance_host(',
        'void chassis_control_clear_cached_intents(']) + test)
    executable = directory / 'can'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-DMOWER_BLADE_OK_ENABLED=1', '-DREMOTE_BACKEND_A28_E34=0', '-DREMOTE_BACKEND_FLYSKY_IBUS=1',
                    '-I'+str(root/'control_chassis/Core/Inc'), str(harness),
                    str(root/'control_chassis/Core/UserDev/mower_mode_control.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
