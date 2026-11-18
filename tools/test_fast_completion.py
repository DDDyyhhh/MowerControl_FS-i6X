#!/usr/bin/env python3
"""Exercise both real adapters, SENS responder and blade controller on one clock."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
protocol = root / 'experiments/ticket03'
core = root / 'control_chassis/Core'
with tempfile.TemporaryDirectory(prefix='mower-fast-completion-') as directory:
    work = Path(directory)
    (work/'main.h').write_text('''#include <stdint.h>
uint32_t HAL_GetTick(void);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
struct FakeSysTick { uint32_t VAL; };
extern struct FakeSysTick fake_systick;
#define SysTick (&fake_systick)
''')
    flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-DEXP3_ENABLED=1', '-DEXP3_PWM_PROBE_ENABLED=1', '-DEXP3_AUTO_SESSION_ENABLED=1',
             '-DMOWER_BLADE_OK_ENABLED=1', '-DREMOTE_BACKEND_A28_E34=0',
             '-DREMOTE_BACKEND_FLYSKY_IBUS=1', '-I'+str(work), '-I'+str(protocol), '-I'+str(core/'Inc')]
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = 'detect_leaks=0'
    def compile_run(name, sources, extra=()):
        subprocess.run(['cc', *flags, *extra, *map(str, sources), '-lm', '-o', str(work/name)], check=True)
        subprocess.run([str(work/name)], env=env, check=True)
    compile_run('codec', [protocol/'test_completion.c', protocol/'exp3.c'])
    # Each board gets its own actual adapter/HIL globals. Only symbol names
    # change; the compiled C bodies are the same as the two firmware builds.
    names = ['g_exp3_hil', 'g_exp3_mailbox', 'g_exp3_downlink_word', 'g_exp3_capture_overflow']
    for file in ['stm32_adapter.c', 'exp3_hil.c']:
        names += re.findall(r'^(?:void|bool|uint32_t|uint16_t)\s+(exp3_\w+)\(', (protocol/file).read_text(), re.M)
    aliases = ['-D'+name+'=controller_'+name for name in names]
    objects = []
    for file in ['stm32_adapter.c', 'exp3_hil.c']:
        obj = work/('controller_'+file+'.o')
        subprocess.run(['cc', *flags, *aliases, '-c', str(protocol/file), '-o', str(obj)], check=True)
        objects.append(obj)
    sensor = work/'sensor.o'
    subprocess.run(['cc', *flags, *aliases, '-c', str(core/'UserDev/sens_ibus_responder.c'), '-o', str(sensor)], check=True)
    objects.append(sensor)
    sources = [protocol/name for name in ['test_completion_cycle.c', 'exp3.c', 'exp3_link.c',
               'exp3_hil.c', 'exp3_auto_session.c', 'exp3_probe.c', 'openi6x_adapter.c', 'exp3_blade_adapter.c']]
    sources += [core/'UserApp/blade_manual_control.c', *objects]
    for driver in ['1U', '2U']:
        compile_run('cycles', sources, ['-DBLADE_MOTOR_DRIVER_USED='+driver])
