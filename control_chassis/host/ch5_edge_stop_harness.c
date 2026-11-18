#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "remote_backend.h"
#include "flysky_ibus_servo.h"
#include "blade_manual_control.h"

static uint32_t now;
static uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
static RemoteCommand_t command;
static RemoteSafetyInput_t safety;

static uint32_t clock_now(void *context)
{
    (void)context;
    return now;
}

static void make_frame(uint8_t bytes[REMOTE_IBUS_FRAME_SIZE])
{
    uint16_t sum = 0U;
    bytes[0] = 32U;
    bytes[1] = 64U;
    for (unsigned i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; ++i)
    {
        bytes[2U + 2U * i] = (uint8_t)channels[i];
        bytes[3U + 2U * i] = (uint8_t)(channels[i] >> 8);
    }
    for (unsigned i = 0U; i < 30U; ++i) sum += bytes[i];
    sum = (uint16_t)(0xffffU - sum);
    bytes[30] = (uint8_t)sum;
    bytes[31] = (uint8_t)(sum >> 8);
}

static void feed(uint16_t ch5, uint16_t ch4)
{
    uint8_t bytes[REMOTE_IBUS_FRAME_SIZE];
    channels[4] = ch5;
    channels[3] = ch4;
    make_frame(bytes);
    now += 10U;
    RemoteBackend_OnUartRx(1U, bytes, sizeof(bytes));
}

static void read_command(void)
{
    assert(RemoteBackend_GetCommand(&command));
}

static void frame(uint16_t ch5, uint16_t ch4)
{
    feed(ch5, ch4);
    RemoteBackend_Advance(now, safety);
    read_command();
}

static void init(uint16_t ch5, uint16_t ch4)
{
    now = 0U;
    safety = (RemoteSafetyInput_t){0};
    for (unsigned i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; ++i) channels[i] = 1500U;
    channels[5] = 1000U;
    channels[6] = channels[7] = 2000U;
    RemoteBackend_Init((RemoteBackendClock_t){clock_now, NULL});
    for (unsigned i = 0U; i < 30U; ++i) frame(ch5, ch4);
    assert(command.stable_online && !command.run_permitted);
}

static void request(uint16_t ch5)
{
    frame(ch5, 1500U);
    frame(ch5, 1000U);
    for (unsigned i = 0U; i < 22U; ++i) frame(ch5, 1000U);
    assert(command.authorization_state == REMOTE_AUTH_WAIT_OK);
}

static void unlock(uint16_t ch5)
{
    request(ch5);
    assert(RemoteBackend_ConfirmUnlock());
    read_command();
    assert(!command.run_permitted);
    frame(ch5, 1000U);
    assert(command.run_permitted && command.remote_run_enable);
}

static void assert_locked(void)
{
    if (command.authorization_state != REMOTE_AUTH_STOPPED_LATCHED)
        fprintf(stderr, "Expected stopped at %lu ms, CH5=%u CH4=%u, state=%u\n",
                (unsigned long)now, channels[4], channels[3], command.authorization_state);
    assert(!command.run_permitted && command.remote_recoverable_latched);
    assert(command.authorization_state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(!RemoteBackend_ConfirmUnlock());
}

static void test_first_frame_and_confirmation_priority(void)
{
    const uint16_t endpoints[] = {1000U, 2000U};
    for (unsigned side = 0U; side < 2U; ++side)
    {
        uint16_t from = endpoints[side], to = endpoints[side ^ 1U];
        for (unsigned phase = 0U; phase < 3U; ++phase)
        {
            init(from, 1500U);
            request(from);
            if (phase > 0U) assert(RemoteBackend_ConfirmUnlock());
            if (phase > 1U) { frame(from, 1000U); assert(command.run_permitted); }
            uint32_t invalidation = command.command_invalidation_sequence;
            /* No poll between reception and OK: ConfirmUnlock must refresh. */
            feed(to, 1000U);
            assert(!RemoteBackend_ConfirmUnlock());
            read_command();
            assert_locked();
            assert(command.command_invalidation_sequence != invalidation);
            assert(command.recoverable_latch_reason == REMOTE_LATCH_REASON_REMOTE_STOP);
            for (unsigned i = 0U; i < 25U; ++i) frame(to, 1000U);
            assert_locked(); /* Holding full and delayed OK cannot replay. */
            unlock(to); /* Both endpoints can independently authorize. */
        }
        init(from, 1500U);
        unlock(from);
        frame(to, 1500U); /* Stop frame must not establish CH4 center. */
        assert_locked();
        frame(to, 1000U);
        assert_locked();
        unlock(to);
    }
}

static void test_overwritten_and_concatenated_frames(void)
{
    const uint16_t endpoints[] = {1000U, 2000U};
    for (unsigned side = 0U; side < 2U; ++side)
    {
        uint16_t from = endpoints[side], to = endpoints[side ^ 1U];
        init(from, 1500U);
        unlock(from);
        feed(to, 1000U);
        feed(from, 1000U); /* Latest value equals old value, event must survive. */
        read_command();
        assert_locked();
        uint32_t invalidation = command.command_invalidation_sequence;
        for (unsigned i = 0U; i < 10U; ++i) read_command();
        assert(command.command_invalidation_sequence == invalidation);

        unlock(from);
        uint8_t bytes[REMOTE_IBUS_FRAME_SIZE * 2U];
        channels[4] = to; make_frame(bytes);
        channels[4] = from; make_frame(bytes + REMOTE_IBUS_FRAME_SIZE);
        now += 10U;
        RemoteBackend_OnUartRx(1U, bytes, sizeof(bytes));
        read_command();
        assert_locked();

        unlock(from);
        feed(1500U, 1000U);
        feed(from, 1000U); /* A brief unknown value also cannot be overwritten. */
        read_command();
        assert_locked();
    }
}

static void test_jitter_unknown_and_bad_frames(void)
{
    const uint16_t stable[][3] = {{900U, 1000U, 1120U}, {1880U, 2000U, 2100U}};
    for (unsigned side = 0U; side < 2U; ++side)
    {
        init(stable[side][1], 1500U);
        unlock(stable[side][1]);
        uint32_t invalidation = command.command_invalidation_sequence;
        for (unsigned i = 0U; i < 3U; ++i)
        {
            frame(stable[side][i], 1000U);
            assert(command.run_permitted);
            assert(command.command_invalidation_sequence == invalidation);
        }
        uint8_t bad[REMOTE_IBUS_FRAME_SIZE];
        channels[4] = stable[side ^ 1U][1];
        make_frame(bad); bad[31] ^= 1U;
        RemoteBackend_OnUartRx(1U, bad, sizeof(bad));
        read_command(); assert(command.run_permitted); /* CRC failure is no edge. */
        channels[4] = 2101U; make_frame(bad);
        RemoteBackend_OnUartRx(1U, bad, sizeof(bad));
        read_command(); assert(command.run_permitted); /* Invalid range is discarded. */
        init(stable[side][1], 1500U);
        unlock(stable[side][1]);
        const uint16_t unknown[] = {1121U, 1500U, 1879U};
        for (unsigned i = 0U; i < 3U; ++i)
        {
            frame(unknown[i], 1000U);
            assert_locked(); assert(!command.remote_run_enable);
            frame(unknown[i], 1500U);
            frame(unknown[i], 1000U);
            assert_locked(); /* No CH4/OK unlock with an unknown CH5. */
        }
    }
}

static void test_recovery(void)
{
    const uint16_t endpoints[] = {1000U, 2000U};
    for (unsigned side = 0U; side < 2U; ++side)
    {
        uint16_t ch5 = endpoints[side];
        init(ch5, 1000U);
        assert(!RemoteBackend_ConfirmUnlock()); /* Startup full is not an edge. */
        unlock(ch5);
        now += REMOTE_IBUS_TIMEOUT_MS;
        RemoteBackend_Advance(now, safety);
        assert(!RemoteBackend_GetCommand(&command));
        assert(!command.run_permitted);
        for (unsigned i = 0U; i < 30U; ++i) frame(ch5, 1000U);
        assert_locked();
        unlock(ch5);
        safety.emergency_active = true;
        frame(ch5, 1000U); assert_locked();
        safety.emergency_active = false;
        for (unsigned i = 0U; i < 25U; ++i) frame(ch5, 1000U);
        assert_locked();
        unlock(ch5);
    }
}

static void update_gate(void)
{
    SafetyOwnershipGateInput_t gate = {0};
    gate.software_locked = !command.run_permitted;
    gate.recoverable_remote_latched = command.remote_recoverable_latched;
    gate.link_stable = command.link_fresh && command.stable_online;
    gate.run_permitted = command.run_permitted;
    gate.selected_owner = command.run_permitted ? EXECUTION_OWNER_MANUAL_REMOTE : EXECUTION_OWNER_NONE;
    gate.command_sequence = command.frame_sequence;
    gate.command_invalidation_sequence = command.command_invalidation_sequence;
    SafetyOwnershipGate_Update(&gate);
}

static BladeManualControlInput_t blade_input(void)
{
    BladeManualControlInput_t input = {0};
    input.link_fresh = command.link_fresh;
    input.timeout_latched = RemoteCommand_TimeoutStopActive(&command);
    input.frame_sequence = command.frame_sequence;
    input.software_locked = !command.run_permitted;
    input.remote_recoverable_latched = command.remote_recoverable_latched;
    input.remote_run_enable = command.remote_run_enable;
    input.run_permitted = command.run_permitted;
    input.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
    input.command_invalidation_sequence = command.command_invalidation_sequence;
    input.gate_stop_requested = !SafetyOwnershipGate_IsOwnerPermitted(EXECUTION_OWNER_MANUAL_REMOTE);
    input.ch4_raw = command.blade_start_raw;
    input.ch7_raw = command.blade_gear_raw;
    input.confirmed_gear = (RemoteBladeGear_t)command.blade_gear;
    return input;
}

static void test_unlock_edge_cannot_start_blade(void)
{
    const uint16_t endpoints[] = {1000U, 2000U};
    for (unsigned side = 0U; side < 2U; ++side) {
        uint16_t ch5 = endpoints[side];
        init(ch5, 1500U);
        SafetyOwnershipGate_Reset(); update_gate();
        BladeManualControl_t blade;
        BladeManualControl_Init(&blade, NULL);
        BladeManualControlInput_t input = blade_input();
        BladeManualControl_Step(&blade, &input); /* Center while locked cannot arm blade. */
        unlock(ch5); update_gate();
        for (unsigned i = 0U; i < 5U; ++i) {
            frame(ch5, 1000U); update_gate(); input = blade_input();
            input.has_confirm_event = true;
            input.confirm_event = (BladeConfirmOK_t){BLADE_PROTOCOL_VERSION, 1U, 1U};
            BladeManualControl_Step(&blade, &input);
            assert(blade.state == BLADE_STATE_IDLE && !blade.start_armed);
            assert(!blade.request_active && blade.commanded_speed == 0.0f);
        }
        frame(ch5, 1500U); update_gate(); input = blade_input();
        BladeManualControl_Step(&blade, &input);
        frame(ch5, 1000U); update_gate(); input = blade_input();
        BladeManualControl_Step(&blade, &input);
        assert(blade.state == BLADE_STATE_START_CONFIRM_PENDING);
        assert(blade.commanded_speed == 0.0f); /* A separate new OK is still required. */
    }
}

static void test_blade_and_drive_stop(void)
{
    const uint16_t endpoints[] = {1000U, 2000U};
    for (unsigned side = 0U; side < 2U; ++side)
    for (unsigned running = 0U; running < 2U; ++running)
    {
        uint16_t ch5 = endpoints[side], other = endpoints[side ^ 1U];
        init(ch5, 1500U); unlock(ch5);
        SafetyOwnershipGate_Reset(); update_gate();
        channels[1] = 2000U;
        frame(ch5, 1500U); update_gate();
        assert(command.throttle != 2048U);
        assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
        BladeManualControl_t blade;
        BladeManualControl_Init(&blade, NULL);
        BladeManualControlInput_t input = blade_input();
        BladeManualControl_Step(&blade, &input);
        frame(ch5, 1000U); update_gate(); input = blade_input();
        BladeManualControl_Step(&blade, &input);
        assert(blade.state == BLADE_STATE_START_CONFIRM_PENDING);
        BladeConfirmOK_t ok = {BLADE_PROTOCOL_VERSION, blade.request_sequence, 1U};
        if (running)
        {
            frame(ch5, 1000U); update_gate(); input = blade_input();
            input.has_confirm_event = true; input.confirm_event = ok;
            BladeManualControl_Step(&blade, &input);
            assert(blade.state == BLADE_STATE_RUNNING && blade.commanded_speed == 0.0f);
            frame(ch5, 1000U); update_gate(); input = blade_input();
            BladeManualControl_Step(&blade, &input);
            assert(blade.commanded_speed > 0.0f);
        }
        frame(other, 1000U); update_gate(); input = blade_input();
        input.has_confirm_event = true; input.confirm_event = ok; /* Simultaneous old OK. */
        BladeManualControl_Step(&blade, &input);
        assert_locked();
        assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
        assert(SafetyOwnershipGate_TakeStopRequest());
        assert(blade.commanded_speed == 0.0f && blade.state == BLADE_STATE_STOPPING);
    }
}

static void test_parser_counter_wrap(void)
{
    FlySkyIbusServo_t ibus;
    FlySkyIbusServo_Init(&ibus, NULL);
    uint8_t bytes[REMOTE_IBUS_FRAME_SIZE];
    init(2000U, 1500U); make_frame(bytes);
    FlySkyIbusServo_Feed(&ibus, bytes, sizeof(bytes), 10U);
    ibus.snapshot.ch5_stop_sequence = UINT32_MAX;
    channels[4] = 1000U; make_frame(bytes);
    FlySkyIbusServo_Feed(&ibus, bytes, sizeof(bytes), 20U);
    FlySkyIbusSnapshot_t snapshot;
    assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
    assert(snapshot.ch5_stop_sequence == 0U);

    RemoteAuthorization_t auth;
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    RemoteAuthorization_Init(&auth, &config);
    RemoteAuthorizationInput_t input = {0};
    input.link_fresh = input.stable_online = true;
    input.steering_raw = input.throttle_raw = input.ch4_raw = 1500U;
    input.run_enable_raw = 2000U;
    input.ch5_stop_sequence = UINT32_MAX;
    input.frame_sequence = 1U;
    RemoteAuthorization_Advance(&auth, &input, 0U);
    input.ch4_raw = 1000U; ++input.frame_sequence;
    RemoteAuthorization_Advance(&auth, &input, 200U);
    assert(RemoteAuthorization_ConfirmOK(&auth, &input, 200U));
    ++input.frame_sequence;
    RemoteAuthorization_Advance(&auth, &input, 210U);
    assert(auth.output.run_permitted);
    input.ch5_stop_sequence = 0U; /* No latest-value change, counter wrapped. */
    ++input.frame_sequence;
    RemoteAuthorization_Advance(&auth, &input, 220U);
    assert(!auth.output.run_permitted);
    assert(auth.output.latch_reason == REMOTE_LATCH_REASON_REMOTE_STOP);
}

int main(void)
{
    test_first_frame_and_confirmation_priority();
    test_overwritten_and_concatenated_frames();
    test_jitter_unknown_and_bad_frames();
    test_recovery();
    test_unlock_edge_cannot_start_blade();
    test_blade_and_drive_stop();
    test_parser_counter_wrap();
    puts("PASS CH5 first-frame bidirectional stop: both-endpoint unlock, overwritten/burst edges, jitter/CRC/ranges, no OK replay, link/safety recovery, drive gate and blade zero");
    return 0;
}
