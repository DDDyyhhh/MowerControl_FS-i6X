#!/usr/bin/env python3
"""Replay a CAN task update between the saved Motion tick and wheel snapshot."""
from pathlib import Path
import ast, os, subprocess, tempfile
root = Path(os.environ.get('MOWER_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
helper = root / 'tools/test_bag_connection_source.py'
ns = {'__file__': str(helper)}
nodes = []
for node in ast.parse(helper.read_text()).body:
    if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'tests' for t in node.targets):
        break
    nodes.append(node)
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(helper), 'exec'), ns)
fixture = ns['fixture']
old = 'static bool chassis_control_read_wheel_feedback(ChassisWheelFeedback_t *out) { (void)out; return false; }'
fixture = fixture.replace(old, """
static uint32_t sample_tick, sampled_now;
static bool chassis_control_read_wheel_feedback(ChassisWheelFeedback_t *out) {
    for(unsigned i=0;i<4;i++) { out[i].valid=true; out[i].last_good_tick=sample_tick; out[i].speed_mm_per_s=500; }
    return true;
}
""")
# The bag fixture supplies a fake clock once the production function needs it.
if 'HAL_GetTick' not in fixture:
    fixture += '\n#define HAL_GetTick() (sampled_now)\n'
else:
    fixture = fixture.replace('static uint32_t telemetry_now;', '').replace('#define HAL_GetTick() telemetry_now', '#define HAL_GetTick() sampled_now')
probe = r'''
int main(void) {
 RemoteCommand_t cmd={0}; RemoteSafetyInput_t safety={0}; TelemetrySnapshot_t s;
 const uint32_t times[][3]={{1000,1001,1001},{1100,950,1100},{1100,949,1100},{0xfffffffeU,0xffffffffU,1U}};
 for(unsigned i=0;i<4;i++) {
  sample_tick=times[i][1]; sampled_now=times[i][2];
  RefreshTelemetrySnapshot(&cmd,&safety,false,times[i][0]); TelemetrySnapshot_Read(&s);
  unsigned stale=!!(s.values[TELEMETRY_VALUE_DETAILED_FAULT]&TELEMETRY_FAULT_WHEEL_STALE);
  unsigned expected=i==2;
  if(stale!=expected) { fprintf(stderr,"FAIL production wheel snapshot: case=%u fresh/150/151/wrap stale=%u expected=%u\n",i,stale,expected); return 1; }
 }
 puts("PASS production wheel snapshot: concurrent +1ms, 150/151ms and uint32 wrap");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-wheel-source-') as d:
 exe=str(Path(d)/'probe')
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'control_chassis/Core/Inc'),'-x','c','-','-x','none',str(root/'control_chassis/Core/UserApp/telemetry_snapshot.c'),'-o',exe], input=fixture+'\n'+ns['body']+'\n'+probe,text=True,check=True)
 subprocess.run([exe],check=True)
