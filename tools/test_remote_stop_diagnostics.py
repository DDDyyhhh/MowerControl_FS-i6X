#!/usr/bin/env python3
"""Execute the production falling-edge recorder and boot initialization."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(text, signature):
    start = text.index(signature)
    opened = text.index('{', start)
    end, depth = opened + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


motion = (root / 'control_chassis/Core/UserApp/app_Motion.c').read_text()
context = (root / 'control_chassis/Core/UserApp/app_context.c').read_text()
fixture = r'''
#include "remote_stop_diagnostics.h"
#include "safety_ownership_gate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    RemoteStopDiagnostics_t remote_stop_diag;
    unsigned lock, physical_safety_latched, g_collision_latched;
    unsigned cfg_collision_en, cfg_tilt_sensor_en, g_system_report_state, knob_height;
} g_app;
static struct { float pitch, roll; } imu_data;
static unsigned Ctrl_Mode, barriers;
static struct { uint32_t IDR; } gpio_d = {0x0c00U};
#define GPIOD (&gpio_d)
static void publication_barrier(void) {
    ++barriers;
    assert(g_app.remote_stop_diag.write_sequence & 1U);
    /* A debugger can stop between any two stores. Invalidate publication
     * before disarming, so a previous valid record cannot look current. */
    if (barriers & 1U) assert(g_app.remote_stop_diag.armed == 1U);
    else assert(g_app.remote_stop_diag.armed == 0U && g_app.remote_stop_diag.valid);
}
#define __DMB() publication_barrier()
static struct { unsigned ccr3,ccr4,period; } htim3={0U,200U,999U};
#define TIM_CHANNEL_3 3U
#define TIM_CHANNEL_4 4U
#define __HAL_TIM_GET_COMPARE(t,c) ((c)==3U?(t)->ccr3:(t)->ccr4)
#define __HAL_TIM_GET_AUTORELOAD(t) ((t)->period)
static RemoteRxDiagnostics_t rx;
void RemoteBackend_GetRxDiagnostics(RemoteRxDiagnostics_t *out) { *out=rx; }
'''
tests = r'''
int main(void) {
    g_app.lock=1U; g_app.cfg_collision_en=g_app.cfg_tilt_sensor_en=1U;
    AppContext_RecordBootReset(0x0c000000U);
    RemoteStopDiagnostics_t *d=&g_app.remote_stop_diag;
    assert(d->magic==REMOTE_STOP_DIAG_MAGIC && d->version==1U && d->record_size==sizeof(*d));
    assert(d->boot_reset_flags==0x0c000000U && !d->valid && !d->armed && g_app.lock==1U);
    RemoteCommand_t c={0}; RemoteSafetyInput_t s={0};
    RecordRemoteStop(&c,&s,false,SAFETY_GATE_REJECT_SOFTWARE_LOCK,20U);
    assert(!d->valid && !d->event_count); // startup lock is not a falling edge
    c.link_fresh=c.connected=c.stable_online=c.run_permitted=true;
    c.timeout_latched=true; // history alone is not a current timeout
    assert(!RemoteCommand_TimeoutStopActive(&c));
    assert(RemoteCommand_TimeoutStopActive(NULL));
    RecordRemoteStop(&c,&s,true,SAFETY_GATE_PERMIT,40U);
    assert(d->armed && !d->valid);
    c.link_fresh=c.connected=c.stable_online=c.run_permitted=false;
    c.remote_recoverable_latched=true; c.recoverable_latch_reason=REMOTE_LATCH_REASON_TIMEOUT;
    c.last_valid_ms=40U; c.frame_sequence=7U; c.timeout_sequence=1U;
    c.ch5_stop_sequence=3U; c.command_invalidation_sequence=9U; c.height_level=4U;
    c.authorization_state=REMOTE_AUTH_SYNCHRONIZING;
    for(unsigned i=0;i<14U;++i)c.channels[i]=(uint16_t)(1000U+i*10U);
    rx.crc_errors=3U; rx.invalid_channel_frames=1U; rx.uart_errors=2U; rx.last_uart_error=8U;
    rx.max_queue_age_ms=107U; rx.queue_overflows=1U;
    g_app.knob_height=4U; imu_data.pitch=2.5f; imu_data.roll=-3.0f;
    RecordRemoteStop(&c,&s,false,SAFETY_GATE_REJECT_SOFTWARE_LOCK,140U);
    assert(d->valid && !d->armed && d->event_count==1U && d->write_sequence==2U && barriers==2U);
    assert(d->captured_ms==140U && d->link_age_ms==100U && d->timeout_sequence==1U);
    assert(d->authorized_ms==40U && d->rx_at_authorization.uart_errors==0U);
    assert(d->latch_reason==REMOTE_LATCH_REASON_TIMEOUT && d->frame_sequence==7U);
    assert(d->input_flags & (1U<<3)); assert(d->input_flags & (1U<<4));
    assert(d->rx.crc_errors==3U && d->rx.uart_errors==2U && d->rx.last_uart_error==8U);
    assert(d->rx.queue_overflows==1U && d->raw_gpiod==0x0c00U);
    assert(d->lifter_pwm_ch3==0U && d->lifter_pwm_ch4==200U && d->lifter_pwm_period==999U);
    assert(d->requested_height_level==4U && d->remote_height_level==4U);
    assert(d->pitch==2.5f && d->roll==-3.0f && d->channels[8]==1080U);
    RemoteStopDiagnostics_t first=*d;
    c.timeout_sequence=2U; rx.uart_errors=99U; s.collision_active=true;
    RecordRemoteStop(&c,&s,false,SAFETY_GATE_REJECT_PHYSICAL,999U);
    assert(memcmp(d,&first,sizeof(first))==0); // later stop/debugger gaps cannot replace first cause
    c.link_fresh=c.connected=c.stable_online=c.run_permitted=true;
    c.remote_recoverable_latched=false; c.recoverable_latch_reason=REMOTE_LATCH_REASON_NONE;
    assert(c.timeout_latched && !RemoteCommand_TimeoutStopActive(&c));
    RecordRemoteStop(&c,&s,true,SAFETY_GATE_PERMIT,1000U);
    assert(d->armed && d->event_count==1U && d->captured_ms==140U);
    assert(d->authorized_ms==1000U && d->rx_at_authorization.uart_errors==99U);
    c.remote_recoverable_latched=true; c.run_permitted=false;
    c.recoverable_latch_reason=REMOTE_LATCH_REASON_REMOTE_STOP;
    RecordRemoteStop(&c,&s,false,SAFETY_GATE_REJECT_REMOTE_LATCH,1010U);
    assert(d->event_count==2U && d->write_sequence==4U && d->captured_ms==1010U);
    assert(!(d->input_flags & (1U<<4)) && (d->input_flags & (1U<<11)));
    assert(d->boot_reset_flags==0x0c000000U);
    c.recoverable_latch_reason=REMOTE_LATCH_REASON_TIMEOUT;
    assert(RemoteCommand_TimeoutStopActive(&c));
    AppContext_RecordBootReset(1U<<29);
    assert(!d->valid && !d->event_count && !d->armed && d->boot_reset_flags==(1U<<29));
    assert(g_app.lock==1U); // boot diagnostics do not change actuator/authorization state
    puts("PASS production stop recorder: startup excluded, first edge frozen, fresh unlock rearms, fields/boot flags/current timeout");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-stop-diag-') as directory:
    work = Path(directory)
    source = fixture + function(context, 'void AppContext_RecordBootReset(') + '\n'
    source += function(motion, 'static void RecordRemoteStop(') + '\n' + tests
    (work / 'test.c').write_text(source)
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-DREMOTE_BACKEND_A28_E34=0',
        '-DREMOTE_BACKEND_FLYSKY_IBUS=1', '-I'+str(root/'control_chassis/Core/Inc'),
        str(work/'test.c'), '-o', str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
