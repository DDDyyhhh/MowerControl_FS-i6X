#!/usr/bin/env python3
"""Exercise the production lifter FSM across permission loss and handoff."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'control_chassis/Core/UserDev/lifter.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start)+1
    depth = 1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}')
        end += 1
    return source[start:end]

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
enum { LIFTER_STATE_IDLE, LIFTER_STATE_HOMING_START, LIFTER_STATE_HOMING_RUNNING,
       LIFTER_STATE_HOMING_DONE, LIFTER_STATE_AUTO_ADJUST };
enum { REPORT_NONE, REPORT_BLADE_INITIALIZING, REPORT_DUMPING_DONE,
       REPORT_DUMPING_START, REPORT_DUMPING_CLOSING };
enum { GRASS_BAG_OPENING, GRASS_BAG_OPEN, GRASS_BAG_CLOSING, GRASS_BAG_CLOSED };
#define LIFTER_SPEED 20.0f
#define HOMING_TIMEOUT_MS 10000U
#define DEAD_BAND_MS 100U
#define UP_SPEED_FACTOR 0.8f
#define FLASH_SAVE_DELAY_MS 10000U
static bool g_height_permitted, g_bag_permitted;
static uint32_t now,g_last_tick,g_homing_start_ts,g_flash_save_timer,g_dump_done_timer;
static float g_current_pos_ms=4000.0f,g_target_pos_ms;
static int8_t g_dir;
static int g_state;
static uint8_t g_flash_save_pending;
static unsigned critical;
#define taskENTER_CRITICAL() (++critical)
#define taskEXIT_CRITICAL() do { assert(critical); --critical; } while(0)
static struct { unsigned g_system_report_state,grass_bag_status; } g_app;
static struct { unsigned state; bool completed_open; } g_bag;
static void *g_bag_mutex;
static int blade_height_bridge;
static float pwm;
static uint32_t HAL_GetTick(void) { return now; }
static void EG2104_HBridge_Stop(int *bridge) { (void)bridge; assert(critical); pwm=0; }
static void EG2104_HBridge_SetSpeed(int *bridge,float speed) { (void)bridge; assert(critical); pwm=speed; }
static void Update_Height_CM_From_MS(float position) { (void)position; }
static void Lifter_SaveHeightToFlash(void) {}
static bool GrassBag_Lock(void) { return false; }
static bool GrassBagActuator_Advance(void *bag,uint32_t at) { (void)bag; (void)at; return false; }
static void GrassBag_Motor_Stop(void) {}
static void osMutexRelease(void *mutex) { (void)mutex; }
'''
test = r'''
int main(void) {
  // An old AUTO_ADJUST target is retired before any further PWM write.
  g_state=LIFTER_STATE_AUTO_ADJUST; g_target_pos_ms=9500; g_dir=-1;
  now=50; Lifter_Task_Update(); assert(g_state==LIFTER_STATE_IDLE && g_dir==0 && pwm==0);
  assert(g_target_pos_ms==g_current_pos_ms && !critical);
  Lifter_SetTargetLevel(1); Lifter_User_Init(); assert(g_state==LIFTER_STATE_IDLE);
  Lifter_SetHeightPermission(true); Lifter_SetTargetLevel(1); now+=50; Lifter_Task_Update();
  assert(g_state==LIFTER_STATE_AUTO_ADJUST && pwm==-20 && g_dir==-1);
  Lifter_SetHeightPermission(false); assert(pwm==0 && g_state==LIFTER_STATE_IDLE);
  now+=50; Lifter_Task_Update(); assert(pwm==0 && g_state==LIFTER_STATE_IDLE);
  // Enabling a new owner cannot restart the old target.
  Lifter_SetHeightPermission(true); now+=50; Lifter_Task_Update(); assert(pwm==0);
  Lifter_SetTargetLevel(5); now+=50; Lifter_Task_Update(); assert(pwm==20 && g_dir==1);
  Lifter_User_Init(); now+=50; Lifter_Task_Update(); assert(g_state==LIFTER_STATE_HOMING_RUNNING);
  Lifter_SetHeightPermission(false); Lifter_SetHeightPermission(true);
  now+=50; Lifter_Task_Update(); assert(g_state==LIFTER_STATE_IDLE && pwm==0 && !critical);
  puts("PASS actual height FSM cancels old targets/homing, blocks locked commands and serializes PWM with permission");
}
'''
start = source.index('static const uint32_t LEVEL_TARGET_MS')
table = source[start:source.index(';',start)+1]
with tempfile.TemporaryDirectory(prefix='mower-height-') as directory:
    directory=Path(directory)
    harness=directory/'height.c'
    harness.write_text(fixture+table+'\n'+'\n'.join(function(signature) for signature in [
        'static void Motor_Up(', 'static void Motor_Down(', 'void Lifter_Stop(',
        'void Lifter_SetHeightPermission(', 'void Lifter_User_Init(',
        'void Lifter_SetTargetLevel(', 'void Lifter_Task_Update('])+test)
    executable=directory/'height'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-DMOWER_BLADE_OK_ENABLED=1',str(harness),'-lm','-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
