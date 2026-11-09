#!/usr/bin/env python3
"""Run the actual locked-state Motion dispatcher with the real i-BUS/backend/auth."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
motion = (root / 'control_chassis/Core/UserApp/app_Motion.c').read_text()
start = motion.index('static void DispatchRemoteConfirmation(')
opened = motion.index('{', start)
end, depth = opened + 1, 1
while depth:
    depth += (motion[end] == '{') - (motion[end] == '}')
    end += 1
fixture = r'''
#include "remote_backend.h"
#include "exp3_probe.h"
#include "battery_monitor.h"
static struct { BatteryMonitor_t battery; } g_app;
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct { uint32_t controller, hmi; uint8_t intent_reason; } g_exp3_hil = {1U, 2U, 0U};
static Exp3PwmEvent inbox, g_motion_blade_event;
static bool ready, g_motion_blade_event_ready, accepted;
static unsigned completions, resets, Ctrl_Mode;
static int g_blade_manual_control;
static uint32_t now, epoch = 1U;
static uint16_t channels[14];
static uint16_t initial_ch5 = 2000U;
void exp3_board_channels(uint32_t at, uint16_t ch4, uint16_t a, uint16_t b) {
    (void)at; (void)ch4; (void)a; (void)b;
}
static RemoteSafetyInput_t safety;
static uint32_t clock_now(void *ctx) { (void)ctx; return now; }
static uint32_t exp3_board_unlock_epoch(void) { return epoch; }
static bool exp3_board_check_intent(const Exp3Message *m, uint32_t at) {
    (void)at; return m->generation==epoch && m->controller==g_exp3_hil.controller && m->hmi==g_exp3_hil.hmi;
}
static void RecordUnlockDecision(const Exp3PwmEvent *e,const RemoteCommand_t *c,bool accepted) {
    (void)e; (void)c; (void)accepted;
}
static void RefreshRemoteIntent(uint32_t at,const RemoteCommand_t *c,RemoteSafetyInput_t s) {
    (void)at; (void)c; (void)s;
}
static bool exp3_board_take_pwm_event(Exp3PwmEvent *event) {
    if (!ready) return false;
    *event = inbox; ready = false; return true;
}
static void exp3_board_complete_pwm_event(const Exp3PwmEvent *event, bool result) {
    (void)event; ++completions; accepted = result;
}
static void BladeManualControl_Reset(int *blade) { (void)blade; ++resets; }
static RemoteCommand_t command;
static void receive(uint16_t ch4, uint32_t at) {
    uint8_t bytes[32] = {32, 64}; uint16_t sum = 0U;
    now = at; channels[3] = ch4;
    for (unsigned i = 0; i < 14; ++i) {
        bytes[2+i*2] = channels[i]; bytes[3+i*2] = channels[i] >> 8;
    }
    for (unsigned i = 0; i < 30; ++i) sum += bytes[i];
    sum = 0xffffU - sum; bytes[30] = sum; bytes[31] = sum >> 8;
    RemoteBackend_OnUartRx(1U, bytes, 32U);
}
static void frame(uint16_t ch4, uint32_t at) {
    receive(ch4, at);
    RemoteBackend_Advance(now, safety); assert(RemoteBackend_GetCommand(&command));
}
static void init(void) {
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    RemoteBackend_InitWithFlySkyConfig((RemoteBackendClock_t){clock_now, NULL}, &config);
    safety = (RemoteSafetyInput_t){0}; now = 0U; epoch = 1U;
    g_app.battery = (BatteryMonitor_t){0}; g_app.battery.data_fresh=true;
    for (unsigned i = 0; i < 14; ++i) channels[i] = 1500U;
    channels[4] = initial_ch5; channels[5] = 1000U; channels[6] = 2000U;
    for (unsigned t = 0U; t <= 400U; t += 10U) frame(1500U, t);
    assert(!command.run_permitted);
    frame(1000U, 410U);
    assert(command.authorization_state == REMOTE_AUTH_WAIT_OK);
    ready = g_motion_blade_event_ready = accepted = false; completions = 0U;
}
static void message(uint32_t generation, uint32_t context) {
    inbox = (Exp3PwmEvent){0}; inbox.kind = EXP3_PWM_CONFIRM;
    inbox.message = (Exp3Message){.controller=1U,.hmi=2U,.context=context,
        .generation=generation,.target=1U,.request=1U,.action=1U,.kind=EXP3_CONFIRM};
    ready = true;
}
'''
tests = r'''
int main(void) {
    init(); message(1U, EXP3_UNLOCK_CONTEXT);
    DispatchRemoteConfirmation(now, safety, &command);
    assert(accepted && completions == 1U && !command.run_permitted);
    assert(!g_motion_blade_event_ready); /* Unlock OK never reaches blade adapter. */
    frame(1000U, 420U); assert(command.run_permitted);
    message(1U, EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now, safety, &command);
    assert(!accepted && command.run_permitted); /* Duplicate cannot relock/re-authorize. */
    init(); g_app.battery.data_fresh=false;
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(accepted && !command.run_permitted && !g_motion_blade_event_ready);
    frame(1000U,420U); assert(command.run_permitted);
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(!accepted && command.run_permitted); /* Feedback loss cannot replay an old OK. */
    message(1U,EXP3_PWM_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(g_motion_blade_event_ready); /* Separate blade OK remains available without BMS. */
    for (unsigned scenario = 0U; scenario < 8U; ++scenario) {
        init(); message(1U, EXP3_UNLOCK_CONTEXT);
        if (scenario == 0) inbox.message.generation = 2U;
        if (scenario == 1) inbox.message.hmi = 3U;
        if (scenario == 2) frame(1500U, 420U);
        if (scenario == 3) { safety.collision_active = true; frame(1000U, 420U); }
        if (scenario == 4) { safety.emergency_active = true; frame(1000U, 420U); }
        if (scenario == 5) { now=600U; RemoteBackend_Advance(now, safety); }
        if (scenario == 6) Ctrl_Mode=2U;
        if (scenario == 7) { inbox.message.context=EXP3_PWM_CONTEXT; }
        DispatchRemoteConfirmation(now, safety, &command);
        assert(!accepted && !command.run_permitted && !g_motion_blade_event_ready);
        assert(completions == 1U);
        Ctrl_Mode=0U;
    }
    init(); g_app.battery.blade_inhibited=true; g_app.battery.recovery_ready=true;
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(accepted); /* A new unlock can recover a healthy battery, never an old OK. */
    init(); message(0U,EXP3_UNLOCK_CONTEXT); inbox.kind=EXP3_PWM_CANCEL;
    DispatchRemoteConfirmation(now,safety,&command);
    assert(accepted && command.authorization_state==REMOTE_AUTH_WAIT_OK);
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(accepted); /* Obsolete Cancel drains without invalidating the current request. */
    init(); g_app.battery.blade_inhibited=true; g_app.battery.low_latched=true;
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(accepted); /* LOW never denies travel unlock. */
    frame(1000U,420U); assert(command.run_permitted);
    init(); g_app.battery.blade_inhibited=true;
    message(1U,EXP3_PWM_CONTEXT); command.run_permitted=true;
    DispatchRemoteConfirmation(now,safety,&command);
    assert(!accepted && completions==1U && !g_motion_blade_event_ready);
    init(); message(1U,EXP3_PWM_CONTEXT); command.run_permitted=true;
    DispatchRemoteConfirmation(now,safety,&command);
    assert(g_motion_blade_event_ready && completions==0U);
    command.run_permitted=false;
    DispatchRemoteConfirmation(now,safety,&command);
    assert(!g_motion_blade_event_ready && completions==1U && !accepted);
    init(); inbox=(Exp3PwmEvent){0}; inbox.kind=EXP3_PWM_CANCEL; ready=true;
    DispatchRemoteConfirmation(now,safety,&command);
    assert(resets && !command.run_permitted);
    message(1U,EXP3_UNLOCK_CONTEXT); DispatchRemoteConfirmation(now,safety,&command);
    assert(!accepted); /* A rebound session requires a new center/full operation. */
    for (unsigned side = 0U; side < 2U; ++side) {
        initial_ch5 = side == 0U ? 1000U : 2000U;
        for (unsigned phase = 0U; phase < 3U; ++phase) {
            init(); message(1U, EXP3_UNLOCK_CONTEXT);
            if (phase > 0U) {
                DispatchRemoteConfirmation(now,safety,&command); assert(accepted);
                if (phase > 1U) { frame(1000U,420U); assert(command.run_permitted); }
                message(1U, EXP3_UNLOCK_CONTEXT);
            }
            channels[4] = side == 0U ? 2000U : 1000U;
            receive(1000U,430U); /* Command cached before the CH5 edge. */
            DispatchRemoteConfirmation(now,safety,&command);
            assert(!accepted);
            assert(RemoteBackend_GetCommand(&command));
            assert(!command.run_permitted && command.remote_recoverable_latched);
            for (unsigned t=440U;t<=700U;t+=10U) frame(1000U,t);
            message(1U, EXP3_UNLOCK_CONTEXT);
            DispatchRemoteConfirmation(now,safety,&command);
            assert(!accepted && !command.run_permitted);
            frame(1500U,710U); frame(1000U,720U);
            for (unsigned t=730U;t<=920U;t+=10U) frame(1000U,t);
            ++epoch; message(epoch, EXP3_UNLOCK_CONTEXT);
            DispatchRemoteConfirmation(now,safety,&command); assert(accepted);
            frame(1000U,930U); assert(command.run_permitted);
        }
    }
    puts("PASS actual 100Hz locked dispatcher + real iBUS/backend: both CH5 endpoints, first-frame stop before/waiting/after OK, no replay, fresh CH4/OK recovery, session/epoch/safety/link/mode rejection, blade isolation");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-unlock-dispatch-') as directory:
    work = Path(directory)
    source = work / 'dispatch.c'
    source.write_text(fixture + motion[start:end] + tests)
    core = root / 'control_chassis/Core'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-DREMOTE_BACKEND_HOST',
                    '-DMOWER_BLADE_OK_ENABLED=1', '-DREMOTE_BACKEND_A28_E34=0',
                    '-DREMOTE_BACKEND_FLYSKY_IBUS=1', '-DEXP3_ENABLED=1',
                    '-I'+str(core/'Inc'), '-I'+str(root/'experiments/ticket03'), str(source),
                    *[str(core/'UserDev'/name) for name in ['remote_backend.c', 'remote_authorization.c',
                        'flysky_ibus_servo.c', 'blade_confirm_ch6_adapter.c']],
                    '-lm', '-o', str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
