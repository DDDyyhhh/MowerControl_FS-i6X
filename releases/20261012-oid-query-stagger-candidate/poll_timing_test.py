"""Production polling function driven at its real 10ms/50ms call schedule.

This tests scheduling, not hardware collision recovery.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
s = (ROOT / 'control_chassis/Core/UserDev/chassis_control.c').read_text()
h = (ROOT / 'control_chassis/Core/Inc/chassis_control.h').read_text()
new_sig = 'void chassis_control_poll_oid_actual_speed(uint8_t phase)'
new = new_sig in s
sig = new_sig if new else 'static void chassis_control_req_oid_actual_speed(void)'
start = s.index(sig)
i = s.index('{', start)
depth, end = 1, i + 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
body = s[start:end]
macros = '\n'.join('#define ' + k + ' ' + re.search(r'#define\s+' + k + r'\s+(\d+)', h).group(1)
                   for k in ('OID_NODE_FL', 'OID_NODE_FR', 'OID_NODE_RL', 'OID_NODE_RR'))
call = ('chassis_control_poll_oid_actual_speed(phase);' if new
        else 'if(phase==5) chassis_control_req_oid_actual_speed();')
code = '#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\n' + macros + '''
static uint32_t now, last[5];
static int count[5], current_calls;
static uint32_t HAL_GetTick(void){return now;}
static int OID_Req_Actual_Speed(uint8_t id){
 assert(id>=1 && id<=4); count[id]++; current_calls++;
 if(last[id])assert(now-last[id]==50); last[id]=now; return 1;
}
''' + body + '''
int main(void){
 for(unsigned step=1;step<=50;step++){
  now=step*10; unsigned phase=(step-1)%5+1; current_calls=0;
''' + call + '''
  assert(current_calls<=1); if(phase==5)assert(current_calls==0);
 }
 for(int id=1;id<=4;id++)assert(count[id]==10);
 puts("PASS: one query per 10ms slot, 50ms per wheel, no query in control slot");
}
'''
with tempfile.TemporaryDirectory() as t:
    c, exe = Path(t) / 'poll.c', Path(t) / 'poll'
    c.write_text(code)
    subprocess.run(['cc', '-std=c99', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
if new:
    motion = (ROOT / 'control_chassis/Core/UserApp/app_Motion.c').read_text()
    assert 'chassis_control_poll_oid_actual_speed(control_divider);' in motion
    assert 'chassis_control_req_oid_actual_speed();' not in s
