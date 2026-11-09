#!/usr/bin/env python3
"""Drive the production bag detector with deterministic GPIO and time inputs."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'control_chassis/Core/UserApp/app_Motion.c').read_text()
start = source.index('      static uint8_t bag_fsm_state = 0;')
end = source.index('      // --- C. 状态判断与执行 ---', start)
detector = source[start:end]
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
enum { GPIO_PIN_RESET=0, GPIO_PIN_SET=1, GPIO_PIN_10=10, GPIO_PIN_11=11,
       REPORT_DUMPING_START=20, REPORT_FAULT_BAG_FULL=19, REPORT_NONE=0 };
#define GPIOD ((void *)0)
static struct {
    unsigned is_bag_full, is_bag_full_valid, is_bag_connected, grass_bag_status;
    unsigned g_collision_latched, g_system_report_state;
} g_app;
static uint32_t tick;
static bool grass, connected;
static uint32_t HAL_GetTick(void) { return tick; }
static int HAL_GPIO_ReadPin(void *port, unsigned pin) {
    assert(port==GPIOD);
    assert(pin==GPIO_PIN_10 || pin==GPIO_PIN_11);
    return pin==GPIO_PIN_11 ? grass : !connected;
}
static void detect(void) {
DETECTOR
}
static void sample(uint32_t now, bool present, bool plugged) {
    tick=now; grass=present; connected=plugged; detect();
    assert(g_app.is_bag_full_valid==plugged);
    assert(g_app.is_bag_connected==plugged);
}
int main(void) {
    assert(!g_app.is_bag_full_valid);
    sample(1000U,false,true); assert(!g_app.is_bag_full);
    sample(2000U,true,true);
    sample(11999U,true,true); assert(!g_app.is_bag_full);
    sample(12000U,true,true); assert(g_app.is_bag_full); // 10s exact
    g_app.grass_bag_status=1U;
    sample(12050U,true,true); assert(!g_app.is_bag_full); // dumping
    g_app.grass_bag_status=0U;
    sample(12100U,true,true); // close, start 14s blanking
    sample(12150U,true,true);
    sample(22150U,true,true); assert(!g_app.is_bag_full);
    sample(26099U,true,true); assert(!g_app.is_bag_full);
    sample(26100U,true,true); assert(g_app.is_bag_full);
    g_app.grass_bag_status=1U; sample(26150U,false,true);
    g_app.grass_bag_status=0U; sample(26200U,false,true);
    sample(26250U,false,true);
    sample(36249U,false,true); assert(!g_app.is_bag_full);
    sample(36250U,false,true); // clear completes the cleaning cycle
    sample(36300U,true,true);
    sample(46300U,true,true); assert(g_app.is_bag_full);
    sample(46350U,true,false); assert(!g_app.is_bag_full);
    sample(46400U,true,true); assert(!g_app.is_bag_full);
    sample(56399U,true,true); assert(!g_app.is_bag_full);
    sample(56400U,true,true); assert(g_app.is_bag_full);
    sample(56500U,false,false);
    sample(0xfffff000U,true,true);
    sample(5903U,true,true); assert(!g_app.is_bag_full);
    sample(5904U,true,true); assert(g_app.is_bag_full); // 32-bit timer wrap
    puts("PASS production bag detector: first sample, 10s full, dumping/clear, 14s blanking, reconnect, timer wrap");
}
'''.replace('DETECTOR', detector)
with tempfile.TemporaryDirectory(prefix='mower-bag-full-source-') as work:
    path = Path(work)
    (path / 'source.c').write_text(fixture)
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', str(path / 'source.c'), '-o', str(path / 'source'),
    ], check=True)
    subprocess.run([str(path / 'source')], check=True)
