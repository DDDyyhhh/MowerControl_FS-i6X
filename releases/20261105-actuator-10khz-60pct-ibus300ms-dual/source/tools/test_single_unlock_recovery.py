#!/usr/bin/env python3
"""Replay physical stops through the production scan, dispatcher and ApplyCommand.

REMOTE_BACKEND_HOST normally removes ApplyCommand, hiding the stale safety cache
between Advance and GetCommand. Enable only those two production blocks here;
all hardware calls are faked, while parser/authorization/command application run.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dispatcher_test = root / 'tools/test_unlock_dispatch.py'
namespace = {'__file__': str(dispatcher_test)}
nodes = []
for node in ast.parse(dispatcher_test.read_text()).body:
    if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == 'tests' for target in node.targets):
        break
    nodes.append(node)
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(dispatcher_test), 'exec'), namespace)

fixture = namespace['fixture'].replace(
    'static struct { BatteryMonitor_t battery; } g_app;', r'''
#include "telemetry_snapshot.h"
#include "safety_ownership_gate.h"
#define RecordRemoteStop(...) ((void)0)
static struct {
    BatteryMonitor_t battery;
    uint16_t Joystick[4];
    unsigned lock, physical_safety_latched, g_collision_latched;
    unsigned collision_reason_latched, emergency_reason_latched, cfg_collision_en, cfg_tilt_sensor_en;
    unsigned g_system_report_state, receiver_status[16], knob_height;
    uint32_t last_receive_timestamp;
    bool communication_connected;
} g_app;
#define REPORT_NONE 0U
#define REPORT_FAULT_COLLISION 1U
#define REPORT_FAULT_EMERGENCY_BTN 2U
#define REPORT_FAULT_TILT 3U
#define STATUS_INDEX_BLADE_SPEED 0U
#define STATUS_INDEX_LOCK 1U
#define RGB_LED_SetColor(a,b) ((void)0)
#define USE_CHASSIS_CONTROL 1
#define GPIOD 0U
#define GPIO_PIN_0 0U
#define GPIO_PIN_RESET 0U
#define BLADE_SPEED_OFF 0U
static bool raw_emergency, raw_collision;
static unsigned stops, blade_stops;
static bool VehicleEmergencyActive(void) { return raw_emergency; }
static unsigned HAL_GPIO_ReadPin(unsigned port, unsigned pin) {
    (void)port; (void)pin; return raw_collision ? 0U : 1U;
}
static void chassis_stop(void) { ++stops; }
static void BladeMotor_SetSpeed(unsigned speed) { assert(speed == 0U); ++blade_stops; }
static void sample_physical_safety(void);
static void RefreshSafetyOwnershipGate(void);
''')
# assert is needed by the hardware fakes before the original fixture includes it.
fixture = '#include <assert.h>\n#include <math.h>\n' + fixture
fixture = fixture.replace(
    'receive(ch4, at);', '''receive(ch4, at);
    sample_physical_safety();''')
fixture = fixture.replace('RemoteBackend_Advance(now, safety); assert(RemoteBackend_GetCommand(&command));',
                          'RefreshSafetyOwnershipGate(); assert(command.valid);')
fixture = fixture.replace('safety = (RemoteSafetyInput_t){0}; now = 0U; epoch = 1U;', '''
    safety = (RemoteSafetyInput_t){0}; now = 0U; epoch = 1U;
    memset(&g_app, 0, sizeof(g_app));
    g_app.lock = g_app.g_collision_latched = g_app.cfg_collision_en = 1U;
    raw_emergency = raw_collision = false; stops = blade_stops = 0U;''')

motion = namespace['motion']
scan_start = motion.index('    // === 2. 安全检测')
scan_end = motion.index('#if REMOTE_BACKEND_A28_E34', scan_start)
physical_scan = 'static void sample_physical_safety(void) {\n' + motion[scan_start:scan_end] + '\n}\n'
refresh_start = motion.index('static void RefreshSafetyOwnershipGate(void)\n{')
refresh_end = motion.index('\nstatic void RefreshTelemetrySnapshot(', refresh_start)
refresh = r'''
static struct { float pitch, roll; } imu_data;
static unsigned last_host_cmd_tick;
#define API_TIMEOUT_MS 100U
static uint16_t g_blade_ok_runtime_word;
static uint32_t HAL_GetTick(void) { return now; }
static void RefreshBatteryMonitor(uint32_t at) { (void)at; }
static void ApplyBatteryAuthorization(void) {}
static void RefreshTelemetrySnapshot(const RemoteCommand_t *current,
                                     const RemoteSafetyInput_t *inputs,
                                     bool available, uint32_t at) {
    (void)available; (void)at;
    command = *current; safety = *inputs;
}
''' + motion[refresh_start:refresh_end]
telemetry_start = motion.index('  sources.emergency_active =')
telemetry_end = motion.index('  sources.tilt_active_or_latched =', telemetry_start)
telemetry = r'''
static uint16_t physical_faults(void) {
    TelemetrySources_t sources = {0}; TelemetrySnapshot_t snapshot;
    const RemoteSafetyInput_t *safety = &g_remote_safety;
    sources.system_locked = g_app.lock != 0U;
''' + motion[telemetry_start:telemetry_end] + r'''
    TelemetrySnapshot_Update(&sources, now);
    assert(TelemetrySnapshot_Read(&snapshot));
    return snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & 0x403U;
}
'''

backend = (root / 'control_chassis/Core/UserDev/remote_backend.c').read_text()
for original in [
        '#if !defined(REMOTE_BACKEND_HOST)\nstatic void RemoteBackend_ApplyCommand(void)',
        '#if !defined(REMOTE_BACKEND_HOST)\n    g_app.communication_connected = g_command.connected;']:
    assert backend.count(original) == 1
    backend = backend.replace(original, original.replace('#if !defined(REMOTE_BACKEND_HOST)', '#if 1'))
backend = backend.replace('"../../../experiments/ticket03/exp3.h"',
                          '"' + str(root / 'experiments/ticket03/exp3.h') + '"')

tests = r'''
static void confirm(void) {
    message(epoch, EXP3_UNLOCK_CONTEXT);
    RefreshSafetyOwnershipGate();
}
static void running(void) {
    init(); assert(physical_faults() == 0x400U);
    confirm(); assert(accepted && !command.run_permitted);
    frame(1000U, 420U); assert(command.run_permitted);
}
static void one_recovery(unsigned reason, bool sequential) {
    running();
    raw_collision = (reason & 2U) != 0U;
    raw_emergency = !sequential && (reason & 1U) != 0U;
    frame(1000U, 430U);
    if (sequential) { raw_emergency = true; frame(1000U, 440U); }
    assert(!command.run_permitted && g_app.physical_safety_latched && stops && blade_stops);
    raw_collision = raw_emergency = false; frame(1000U, 450U);
    assert(g_app.physical_safety_latched && !command.run_permitted);
    ++epoch; frame(1500U, 460U); frame(1000U, 470U);
    for (unsigned at=480U; at<=680U; at+=10U) frame(1000U, at);
    confirm(); assert(accepted && !command.run_permitted && g_app.physical_safety_latched);
    message(epoch, EXP3_UNLOCK_CONTEXT); /* duplicate arrives on the first post-OK frame */
    frame(1000U, 690U);
    if (!command.run_permitted) {
        fprintf(stderr, "FAIL physical stop %u: first OK accepted and latch cleared=%u, "
                "but Advance -> ApplyCommand -> GetCommand revoked permission\n",
                reason, !g_app.physical_safety_latched);
        exit(1);
    }
    for (unsigned read=0U; read<8U; ++read) {
        assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    }
    assert(!accepted); /* duplicate neither reauthorizes nor revokes */
    assert(!g_app.lock && !g_app.physical_safety_latched && !g_app.g_collision_latched);
    assert(!g_app.collision_reason_latched && !g_app.emergency_reason_latched);
    assert(physical_faults() == 0U);
    confirm(); assert(!accepted && command.run_permitted); /* duplicate stays inert */
    frame(1000U, 700U);
    assert(SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_MANUAL_REMOTE));
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    /* A genuinely new pulse, even after the physical input rebounds, must stop. */
    raw_emergency = true; frame(1000U, 710U);
    raw_emergency = false; frame(1000U, 720U);
    assert(!command.run_permitted && g_app.physical_safety_latched);
    assert(physical_faults() == 0x401U);
}
int main(void) {
    for (unsigned reason=1U; reason<=3U; ++reason) one_recovery(reason, false);
    one_recovery(3U, true); /* emergency occurs after collision already latched */
    for (unsigned reason=1U; reason<=2U; ++reason) {
        running(); raw_emergency = reason == 1U; raw_collision = reason == 2U;
        frame(1000U, 430U);
        assert(physical_faults() == (0x400U | reason));
        ++epoch; frame(1500U, 440U); frame(1000U, 450U);
        for (unsigned at=460U; at<=680U; at+=10U) frame(1000U, at);
        confirm(); assert(!accepted && !command.run_permitted);
        raw_emergency = raw_collision = false; frame(1000U, 690U);
        assert(physical_faults() == (0x400U | reason)); /* released cause remains */
        confirm(); assert(!accepted && !command.run_permitted); /* no deferred OK */
    }
    puts("PASS production physical scan -> real iBUS/dispatcher -> Advance/Apply/Get: "
         "single recovery, repeated reads, duplicate/held OK, released/sequential causes, new stop");
}
'''

with tempfile.TemporaryDirectory(prefix='mower-single-unlock-') as directory:
    work = Path(directory)
    source = work / 'recovery.c'
    source.write_text('#include <stdlib.h>\n' + fixture + backend + physical_scan + telemetry +
                      motion[namespace['start']:namespace['end']] + refresh + tests)
    core = root / 'control_chassis/Core'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-DREMOTE_BACKEND_HOST',
                    '-DMOWER_BLADE_OK_ENABLED=1', '-DREMOTE_BACKEND_A28_E34=0',
                    '-DREMOTE_BACKEND_FLYSKY_IBUS=1', '-DEXP3_ENABLED=1',
                    '-DEXP3_PWM_PROBE_ENABLED=1',
                    '-I'+str(core/'Inc'), '-I'+str(root/'experiments/ticket03'), str(source),
                    *[str(core/'UserDev'/name) for name in ['remote_authorization.c',
                        'flysky_ibus_servo.c', 'blade_confirm_ch6_adapter.c']],
                    str(core/'UserApp/telemetry_snapshot.c'), str(core/'UserApp/safety_ownership_gate.c'),
                    '-lm', '-o', str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
