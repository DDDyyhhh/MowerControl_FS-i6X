#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "blade_manual_control.h"
#include "BladeMotor.h"
#include "safety_ownership_gate.h"

static BladeManualControlInput_t SafeRunningInput(uint32_t frame)
{
    BladeManualControlInput_t input;
    memset(&input, 0, sizeof(input));

    input.link_fresh = true;
    input.timeout_latched = false;
    input.frame_sequence = frame;

    input.physical_latched = false;
    input.emergency_active = false;
    input.collision_active = false;
    input.tilt_active = false;
    input.software_locked = false;
    input.remote_recoverable_latched = false;
    input.remote_run_enable = true;
    input.run_permitted = true;
    input.run_enable_changed = false;
    input.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
    input.owner_changed = false;
    input.command_invalidation_sequence = 1U;
    input.command_invalidated = false;
    input.gate_stop_requested = false;

    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 2000U;
    input.confirmed_gear = REMOTE_BLADE_GEAR_LOW;

    return input;
}

static void HelperStartBlade(BladeManualControl_t *ctrl, uint32_t *frame_counter)
{
    BladeManualControlInput_t input = SafeRunningInput(++(*frame_counter));

    /* 1. Center baseline */
    input.ch4_position = BLADE_CH4_POS_CENTER;
    input.ch7_raw = 1000U;
    BladeManualControl_Step(ctrl, &input);
    assert(BladeManualControl_GetState(ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_IsCenterBaselineObserved(ctrl));
    assert(BladeManualControl_GetCommand(ctrl) == 0.0f);

    /* 2. Start edge -> PENDING */
    input = SafeRunningInput(++(*frame_counter));
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 1000U;
    BladeManualControl_Step(ctrl, &input);
    assert(BladeManualControl_GetState(ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(ctrl) == 0.0f);
    uint32_t req_seq = BladeManualControl_GetRequestSequence(ctrl);
    assert(req_seq > 0U);

    /* 3. Confirm OK -> RUNNING (output still 0 in confirm frame) */
    input = SafeRunningInput(++(*frame_counter));
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = req_seq;
    input.confirm_event.event_sequence = 100U + *frame_counter;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(ctrl, &input);
    assert(BladeManualControl_GetState(ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(ctrl) == 0.0f);

    /* 4. Next frame -> CH7 speed active */
    input = SafeRunningInput(++(*frame_counter));
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(ctrl, &input);
    assert(BladeManualControl_GetState(ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(ctrl) > 0.0f);
}

/* 1. Power-up at start position prohibited from starting */
static void TestPowerUpAtStartProhibited(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    BladeManualControlInput_t input = SafeRunningInput(1U);
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 2000U;

    /* Advance multiple frames while at START */
    for (uint32_t f = 1U; f <= 5U; f++)
    {
        input.frame_sequence = f;
        BladeManualControl_Step(&ctrl, &input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
        assert(!BladeManualControl_IsCenterBaselineObserved(&ctrl));
        assert(BladeManualControl_GetRequestSequence(&ctrl) == 0U);
    }

    /* Move to center -> establishes baseline, but remains in IDLE */
    input.frame_sequence = 6U;
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
    assert(BladeManualControl_IsCenterBaselineObserved(&ctrl));

    /* Now transition to START -> new start edge creates pending request */
    input.frame_sequence = 7U;
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 1U);
}

/* 2. Pending output is strictly zero */
static void TestPendingOutputStrictlyZero(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    uint32_t frame = 0U;
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);

    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);

    uint16_t test_ch7[] = {1000U, 1500U, 2000U, 2012U};
    for (size_t i = 0; i < sizeof(test_ch7) / sizeof(test_ch7[0]); i++)
    {
        input = SafeRunningInput(++frame);
        input.ch4_position = BLADE_CH4_POS_START;
        input.ch7_raw = test_ch7[i];
        input.blade_speed_override = 50.0f;
        BladeManualControl_Step(&ctrl, &input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

        BladeStartConfirmRequest_t req;
        assert(BladeManualControl_GetStartConfirmRequest(&ctrl, &req));
        assert(req.version == BLADE_PROTOCOL_VERSION);
        assert(req.state == BLADE_STATE_START_CONFIRM_PENDING);
        assert(req.request_sequence == 1U);
    }
}

/* 3. Stale / duplicate / mismatched OK rejected */
static void TestStaleDuplicateMismatchedOkRejected(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    uint32_t frame = 0U;
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);

    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    uint32_t req_seq = BladeManualControl_GetRequestSequence(&ctrl);
    assert(req_seq == 1U);

    /* Case A: Wrong version */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = 99U;
    input.confirm_event.request_sequence = req_seq;
    input.confirm_event.event_sequence = 10U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Case B: Wrong request sequence */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = req_seq + 1U;
    input.confirm_event.event_sequence = 11U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Case C: Stale event sequence (older than baseline) */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = req_seq;
    input.confirm_event.event_sequence = 0U; /* baseline is 0 */
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Case D: Matching fresh OK accepted */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = req_seq;
    input.confirm_event.event_sequence = 12U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f); /* 0 on confirm frame */

    /* Case E: Replay duplicate event sequence 12 */
    /* First return to STOPPING then IDLE */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);

    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);

    /* Start new request -> req_seq becomes 2 */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 2U);

    /* Try injecting duplicate event 12 with request_sequence 2 -> rejected */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 2U;
    input.confirm_event.event_sequence = 12U; /* already consumed */
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Try older event 5 -> rejected */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 2U;
    input.confirm_event.event_sequence = 5U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Fresh event 13 -> accepted */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 2U;
    input.confirm_event.event_sequence = 13U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
}

/* 4. CH7 anti-replay */
static void TestCh7AntiReplay(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    uint32_t frame = 10U;
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);

    /* In PENDING, CH7 is 2000 */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* OK arrives in frame 13 with CH7 still at 2000 */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 1U;
    input.confirm_event.event_sequence = 1U;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    /* In confirmation frame, output MUST BE ZERO */
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* A fresh post-confirmation frame selects the high gear. */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 1000U;
    input.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) == ctrl.config.output_max);

    /* The next confirmed low gear updates the output. */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) == ctrl.config.output_min);

    /* Trigger stop */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* In IDLE, CH7 = 2000 produces zero */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
}

/* 5. CH4 single-frame center stop (no debounce) */
static void TestCh4SingleFrameCenterStop(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    uint32_t frame = 0U;
    HelperStartBlade(&ctrl, &frame);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) > 0.0f);

    /* Exactly ONE frame at center stops blade immediately */
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Next frame transitions to IDLE */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
    assert(BladeManualControl_IsCenterBaselineObserved(&ctrl));

    /* Test CH4 center while in PENDING */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);

    /* Single frame at center cancels pending immediately */
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
}

/* 6. All safety stop conditions */
typedef struct
{
    const char *name;
    void (*apply_fault)(BladeManualControlInput_t *input);
} SafetyFaultTestCase_t;

static void FaultLinkTimeout(BladeManualControlInput_t *in) { in->link_fresh = false; }
static void FaultTimeoutLatched(BladeManualControlInput_t *in) { in->timeout_latched = true; }
static void FaultRunEnableChanged(BladeManualControlInput_t *in) { in->run_enable_changed = true; }
static void FaultRunEnableOff(BladeManualControlInput_t *in) { in->remote_run_enable = false; }
static void FaultRunNotPermitted(BladeManualControlInput_t *in) { in->run_permitted = false; }
static void FaultPhysicalLatched(BladeManualControlInput_t *in) { in->physical_latched = true; }
static void FaultEmergencyActive(BladeManualControlInput_t *in) { in->emergency_active = true; }
static void FaultCollisionActive(BladeManualControlInput_t *in) { in->collision_active = true; }
static void FaultTiltActive(BladeManualControlInput_t *in) { in->tilt_active = true; }
static void FaultSoftwareLocked(BladeManualControlInput_t *in) { in->software_locked = true; }
static void FaultRemoteLatched(BladeManualControlInput_t *in) { in->remote_recoverable_latched = true; }
static void FaultOwnerApi(BladeManualControlInput_t *in) { in->selected_owner = EXECUTION_OWNER_API; }
static void FaultOwnerNone(BladeManualControlInput_t *in) { in->selected_owner = EXECUTION_OWNER_NONE; }
static void FaultOwnerChanged(BladeManualControlInput_t *in) { in->owner_changed = true; }
static void FaultCommandInvalidated(BladeManualControlInput_t *in) { in->command_invalidated = true; }
static void FaultCommandInvalidationSeq(BladeManualControlInput_t *in) { in->command_invalidation_sequence = 99U; }
static void FaultGateStopRequested(BladeManualControlInput_t *in) { in->gate_stop_requested = true; }

static const SafetyFaultTestCase_t kSafetyFaultTests[] = {
    {"link_fresh = false", FaultLinkTimeout},
    {"timeout_latched = true", FaultTimeoutLatched},
    {"run_enable_changed = true", FaultRunEnableChanged},
    {"remote_run_enable = false", FaultRunEnableOff},
    {"run_permitted = false", FaultRunNotPermitted},
    {"physical_latched = true", FaultPhysicalLatched},
    {"emergency_active = true", FaultEmergencyActive},
    {"collision_active = true", FaultCollisionActive},
    {"tilt_active = true", FaultTiltActive},
    {"software_locked = true", FaultSoftwareLocked},
    {"remote_recoverable_latched = true", FaultRemoteLatched},
    {"selected_owner = API", FaultOwnerApi},
    {"selected_owner = NONE", FaultOwnerNone},
    {"owner_changed = true", FaultOwnerChanged},
    {"command_invalidated = true", FaultCommandInvalidated},
    {"command_invalidation_sequence changed", FaultCommandInvalidationSeq},
    {"gate_stop_requested = true", FaultGateStopRequested},
};

static void TestAllSafetyStopsAndNoAutoResume(void)
{
    size_t num_tests = sizeof(kSafetyFaultTests) / sizeof(kSafetyFaultTests[0]);

    for (size_t i = 0; i < num_tests; i++)
    {
        BladeManualControl_t ctrl;
        BladeManualControl_Init(&ctrl, NULL);

        uint32_t frame = 0U;
        HelperStartBlade(&ctrl, &frame);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
        assert(BladeManualControl_GetCommand(&ctrl) > 0.0f);

        /* Apply the safety fault on frame N */
        BladeManualControlInput_t fault_input = SafeRunningInput(++frame);
        kSafetyFaultTests[i].apply_fault(&fault_input);
        BladeManualControl_Step(&ctrl, &fault_input);

        /* Must immediately enter STOPPING with zero output */
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

        /* Frame N+1: transitions to IDLE */
        BladeManualControlInput_t next_input = fault_input;
        next_input.frame_sequence = ++frame;
        BladeManualControl_Step(&ctrl, &next_input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

        /* Recovery test: remove fault while CH4 is STILL at START */
        BladeManualControlInput_t recovered_input = SafeRunningInput(++frame);
        recovered_input.ch4_position = BLADE_CH4_POS_START;
        recovered_input.ch7_raw = 2000U;
        BladeManualControl_Step(&ctrl, &recovered_input);

        /* Invariant: MUST NOT resume RUNNING or PENDING! Output MUST remain 0.0f */
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
        assert(!BladeManualControl_IsCenterBaselineObserved(&ctrl));

        /* Prove that full restart sequence is required:
         * 1. Move CH4 to CENTER to re-establish baseline */
        recovered_input.frame_sequence = ++frame;
        recovered_input.ch4_position = BLADE_CH4_POS_CENTER;
        BladeManualControl_Step(&ctrl, &recovered_input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
        assert(BladeManualControl_IsCenterBaselineObserved(&ctrl));

        /* 2. New start edge */
        recovered_input.frame_sequence = ++frame;
        recovered_input.ch4_position = BLADE_CH4_POS_START;
        BladeManualControl_Step(&ctrl, &recovered_input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
        uint32_t new_req = BladeManualControl_GetRequestSequence(&ctrl);

        /* 3. New matching OK */
        recovered_input.frame_sequence = ++frame;
        recovered_input.has_confirm_event = true;
        recovered_input.confirm_event.version = BLADE_PROTOCOL_VERSION;
        recovered_input.confirm_event.request_sequence = new_req;
        recovered_input.confirm_event.event_sequence = 1000U + frame;
        BladeManualControl_Step(&ctrl, &recovered_input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
        assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

        /* 4. New CH7 command */
        recovered_input.frame_sequence = ++frame;
        recovered_input.has_confirm_event = false;
        recovered_input.ch7_raw = 2000U;
        BladeManualControl_Step(&ctrl, &recovered_input);
        assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
        assert(BladeManualControl_GetCommand(&ctrl) > 0.0f);
    }
}

/* 7. Sequence & tick wraparound */
static void TestSequenceWraparound(void)
{
    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    /* Test request_sequence wraparound from 0xFFFFFFFF to 1 */
    ctrl.request_sequence = 0xFFFFFFFFU;
    ctrl.center_baseline_observed = true;
    ctrl.start_armed = true;
    ctrl.last_ch4_position = BLADE_CH4_POS_CENTER;

    uint32_t frame = 100U;
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);

    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 1U);

    /* Test event_sequence wraparound across 0xFFFFFFFF boundary */
    ctrl.baseline_event_sequence = 0xFFFFFFFEU;
    ctrl.last_consumed_event_sequence = 0xFFFFFFFEU;

    /* Stale event 0xFFFFFFFDU: (int32_t)(0xFFFFFFFD - 0xFFFFFFFE) = -1 < 0 -> rejected */
    input = SafeRunningInput(++frame);
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 1U;
    input.confirm_event.event_sequence = 0xFFFFFFFDU;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);

    /* Fresh wrapped event 1U: (int32_t)(1 - 0xFFFFFFFE) = 3 > 0 -> accepted */
    input = SafeRunningInput(++frame);
    input.has_confirm_event = true;
    input.confirm_event.version = BLADE_PROTOCOL_VERSION;
    input.confirm_event.request_sequence = 1U;
    input.confirm_event.event_sequence = 1U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    /* Test frame_sequence wraparound across 0xFFFFFFFF boundary */
    ctrl.confirmed_frame_sequence = 0xFFFFFFFFU;
    input = SafeRunningInput(0U); /* frame sequence wrapped to 0 */
    input.has_confirm_event = false;
    input.ch7_raw = 2000U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&ctrl) > 0.0f);
}

/* 8. CH7 three-position mapping and invalid input */
static void TestCh7GearMapping(void)
{
    BladeManualControlConfig_t config = BladeManualControl_DefaultConfig();
    assert(BladeManualControl_MapSpeed(&config, 2000U) == config.output_min);
    assert(BladeManualControl_MapSpeed(&config, 1500U) == config.output_medium);
    assert(BladeManualControl_MapSpeed(&config, 1000U) == config.output_max);
    assert(BladeManualControl_MapSpeed(&config, 2012U) == config.output_min);
    assert(BladeManualControl_MapSpeed(&config, 1800U) == 0.0f);
    assert(BladeManualControl_MapSpeed(&config, 900U) == config.output_max);
}

static void TestInvalidCh7StopsAndRequiresNewConfirmation(void)
{
    BladeManualControl_t ctrl;
    uint32_t frame = 0U;
    BladeManualControl_Init(&ctrl, NULL);
    HelperStartBlade(&ctrl, &frame);

    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.ch7_raw = 1800U;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    input = SafeRunningInput(++frame);
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    input = SafeRunningInput(++frame);
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);

    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    input = SafeRunningInput(++frame);
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
}

/* 9. CH4 Classification */
static void TestCh4Classification(void)
{
    BladeManualControlConfig_t config = BladeManualControl_DefaultConfig();
    config.ch4_center_us = 1500U;
    config.ch4_start_us = 2000U;
    config.ch4_tolerance_us = 100U;

    assert(BladeManualControl_ClassifyCh4(&config, 1500U) == BLADE_CH4_POS_CENTER);
    assert(BladeManualControl_ClassifyCh4(&config, 1420U) == BLADE_CH4_POS_CENTER);
    assert(BladeManualControl_ClassifyCh4(&config, 1580U) == BLADE_CH4_POS_CENTER);

    assert(BladeManualControl_ClassifyCh4(&config, 2000U) == BLADE_CH4_POS_START);
    assert(BladeManualControl_ClassifyCh4(&config, 1920U) == BLADE_CH4_POS_START);
    assert(BladeManualControl_ClassifyCh4(&config, 2080U) == BLADE_CH4_POS_START);

    assert(BladeManualControl_ClassifyCh4(&config, 1000U) == BLADE_CH4_POS_OTHER);
    assert(BladeManualControl_ClassifyCh4(&config, 1750U) == BLADE_CH4_POS_OTHER);
}

/* 10. End-to-end integration with SafetyOwnershipGate */
static void TestIntegrationWithSafetyOwnershipGate(void)
{
    SafetyOwnershipGateInput_t gate_in = {
        false, false, false, true, true, EXECUTION_OWNER_MANUAL_REMOTE, 1U, 0U};
    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&gate_in);

    BladeManualControl_t ctrl;
    BladeManualControl_Init(&ctrl, NULL);

    uint32_t frame = 0U;
    HelperStartBlade(&ctrl, &frame);
    float cmd = BladeManualControl_GetCommand(&ctrl);
    assert(cmd > 0.0f);

    /* Gate authorizes nonzero intent */
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    /* Lock software in gate */
    gate_in.software_locked = true;
    SafetyOwnershipGate_Update(&gate_in);

    /* Gate denies nonzero intent and flags stop */
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    /* Blade manual control detects software lock or gate stop request */
    BladeManualControlInput_t input = SafeRunningInput(++frame);
    input.software_locked = true;
    input.gate_stop_requested = true;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    assert(BladeManualControl_GetCommand(&ctrl) == 0.0f);
}

static void TestBladeHmiContractValidation(void)
{
    /* 1. BladeStartConfirmRequest validation */
    BladeStartConfirmRequest_t req = {0};
    assert(!BladeHmi_IsStartConfirmRequestValid(&req));

    req.version = BLADE_PROTOCOL_VERSION;
    req.state = BLADE_STATE_START_CONFIRM_PENDING;
    req.request_sequence = 1U;
    assert(BladeHmi_IsStartConfirmRequestValid(&req));

    req.state = BLADE_STATE_RUNNING;
    assert(!BladeHmi_IsStartConfirmRequestValid(&req)); /* Not pending */

    req.state = BLADE_STATE_START_CONFIRM_PENDING;
    req.version = 99U;
    assert(!BladeHmi_IsStartConfirmRequestValid(&req)); /* Wrong version */

    req.version = BLADE_PROTOCOL_VERSION;
    req.request_sequence = 0U;
    assert(!BladeHmi_IsStartConfirmRequestValid(&req)); /* Zero sequence */

    /* 2. BladeConfirmOK validation */
    BladeConfirmOK_t ok;
    ok.version = BLADE_PROTOCOL_VERSION;
    ok.request_sequence = 5U;
    ok.event_sequence = 10U;

    /* Expected req_seq 5, baseline 9, last_consumed 9 -> valid */
    assert(BladeHmi_IsConfirmOKValid(&ok, 5U, 9U, 9U));

    /* Version mismatch */
    ok.version = 2U;
    assert(!BladeHmi_IsConfirmOKValid(&ok, 5U, 9U, 9U));
    ok.version = BLADE_PROTOCOL_VERSION;

    /* Request sequence mismatch */
    assert(!BladeHmi_IsConfirmOKValid(&ok, 6U, 9U, 9U));

    /* Stale event (at or before baseline) */
    assert(!BladeHmi_IsConfirmOKValid(&ok, 5U, 10U, 0U));
    assert(!BladeHmi_IsConfirmOKValid(&ok, 5U, 11U, 0U));

    /* Already consumed event */
    assert(!BladeHmi_IsConfirmOKValid(&ok, 5U, 0U, 10U));
    assert(!BladeHmi_IsConfirmOKValid(&ok, 5U, 0U, 12U));
}

static void TestCh4StartArmingAcrossOtherSamples(void)
{
    BladeManualControl_t ctrl;
    BladeManualControlInput_t input;
    uint32_t frame = 0U;

    /* CENTER -> OTHER -> OTHER -> START must create one request. */
    BladeManualControl_Init(&ctrl, NULL);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_OTHER;
    BladeManualControl_Step(&ctrl, &input);
    input.frame_sequence = ++frame;
    BladeManualControl_Step(&ctrl, &input);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 1U);

    /* Holding START must not create another request. */
    for (uint32_t i = 0U; i < 5U; i++)
    {
        input = SafeRunningInput(++frame);
        input.ch4_position = BLADE_CH4_POS_START;
        BladeManualControl_Step(&ctrl, &input);
        assert(BladeManualControl_GetRequestSequence(&ctrl) == 1U);
    }

    /* Power-up OTHER -> START without CENTER must not arm. */
    BladeManualControl_Init(&ctrl, NULL);
    frame = 0U;
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_OTHER;
    BladeManualControl_Step(&ctrl, &input);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 0U);

    /* Direct CENTER -> START remains valid. */
    BladeManualControl_Init(&ctrl, NULL);
    frame = 0U;
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_CENTER;
    BladeManualControl_Step(&ctrl, &input);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_START_CONFIRM_PENDING);
    assert(BladeManualControl_GetRequestSequence(&ctrl) == 1U);

    /* A safety stop disarms; OTHER -> START cannot restart without CENTER. */
    BladeManualControl_Init(&ctrl, NULL);
    frame = 0U;
    HelperStartBlade(&ctrl, &frame);
    input = SafeRunningInput(++frame);
    input.timeout_latched = true;
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_STOPPING);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_OTHER;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
    input = SafeRunningInput(++frame);
    input.ch4_position = BLADE_CH4_POS_START;
    BladeManualControl_Step(&ctrl, &input);
    assert(BladeManualControl_GetState(&ctrl) == BLADE_STATE_IDLE);
}

int main(void)
{
    printf("Running blade_manual_control_harness...\n");

    TestPowerUpAtStartProhibited();
    TestPendingOutputStrictlyZero();
    TestStaleDuplicateMismatchedOkRejected();
    TestCh7AntiReplay();
    TestCh4SingleFrameCenterStop();
    TestAllSafetyStopsAndNoAutoResume();
    TestSequenceWraparound();
    TestCh7GearMapping();
    TestInvalidCh7StopsAndRequiresNewConfirmation();
    TestCh4Classification();
    TestIntegrationWithSafetyOwnershipGate();
    TestBladeHmiContractValidation();
    TestCh4StartArmingAcrossOtherSamples();

    printf("All blade_manual_control_harness tests PASSED successfully!\n");
    return 0;
}
