#include "exp3_blade_adapter.h"
#include "blade_manual_control.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Exp3PwmEvent confirm_event(void)
{
    Exp3PwmEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = EXP3_PWM_CONFIRM;
    event.message.action = 11U;
    event.message.context = EXP3_PWM_CONTEXT;
    event.message.target = EXP3_PWM_TARGET;
    event.message.generation = 1U;
    event.ch4_epoch = 1U;
    return event;
}

static void accepted_result_crosses_existing_blade_state_machine_once(void)
{
    BladeManualControl_t blade;
    BladeManualControlInput_t input = {0};
    BladeConfirmOK_t ok;
    Exp3BladeInput bridge = {0};
    Exp3BladeDecision decision;

    BladeManualControl_Init(&blade, NULL);
    input.link_fresh = true;
    input.remote_run_enable = true;
    input.run_permitted = true;
    input.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
    input.frame_sequence = 1U;
    input.ch4_raw = 1500U;
    input.ch7_raw = 2000U;
    input.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
    BladeManualControl_Step(&blade, &input);
    input.frame_sequence++;
    input.ch4_raw = 988U;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetState(&blade) == BLADE_STATE_START_CONFIRM_PENDING);

    bridge.event = confirm_event();
    bridge.blade_state = BladeManualControl_GetState(&blade);
    bridge.ch4_raw = input.ch4_raw;
    bridge.gate_permitted = true;
    bridge.request_sequence = BladeManualControl_GetRequestSequence(&blade);
    bridge.ch4_epoch = 1U;
    bridge.baseline_event_sequence = blade.baseline_event_sequence;
    bridge.last_consumed_event_sequence = blade.last_consumed_event_sequence;
    decision = exp3_blade_adapter_decide(&bridge);
    assert(decision.inject_confirm);
    assert(decision.request_sequence == bridge.request_sequence);

    memset(&ok, 0, sizeof(ok));
    ok.version = BLADE_PROTOCOL_VERSION;
    ok.request_sequence = decision.request_sequence;
    ok.event_sequence = decision.event_sequence;
    BladeManualControl_InjectConfirmOK(&blade, &ok);
    input.frame_sequence++;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetState(&blade) == BLADE_STATE_RUNNING);
    assert(BladeManualControl_GetCommand(&blade) == 0.0f);
    input.frame_sequence++;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetCommand(&blade) == 45.0f);

    input.ch7_raw = 1500U;
    input.confirmed_gear = REMOTE_BLADE_GEAR_MEDIUM;
    input.frame_sequence++;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetCommand(&blade) == 50.0f);

    input.ch7_raw = 1000U;
    input.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
    input.frame_sequence++;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetCommand(&blade) == 55.0f);

    input.link_fresh = false;
    input.frame_sequence++;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetCommand(&blade) == 0.0f);

    bridge.blade_state = BLADE_STATE_RUNNING;
    assert(!exp3_blade_adapter_decide(&bridge).inject_confirm);
}

static void denied_or_cancelled_results_fail_closed(void)
{
    Exp3BladeInput input = {0};
    input.event = confirm_event();
    input.blade_state = BLADE_STATE_START_CONFIRM_PENDING;
    input.ch4_raw = 988U;
    input.request_sequence = 9U;
    input.ch4_epoch = 1U;
    assert(exp3_blade_adapter_decide(&input).reason == EXP3_BLADE_DENY_GATE);
    input.gate_permitted = true;
    input.ch4_raw = 1500U;
    assert(!exp3_blade_adapter_decide(&input).inject_confirm);
    input.event.kind = EXP3_PWM_CANCEL;
    assert(exp3_blade_adapter_decide(&input).invalidate);

    input.event = confirm_event();
    input.ch4_raw = 988U;
    input.event.message.target = 9U;
    assert(!exp3_blade_adapter_decide(&input).inject_confirm);
    input.event = confirm_event();
    input.ch4_epoch = 2U;
    assert(exp3_blade_adapter_decide(&input).reason == EXP3_BLADE_DENY_GENERATION);
    input.event.kind = EXP3_PWM_CANCEL;
    input.event.message.kind = EXP3_RESULT;
    assert(!exp3_blade_adapter_decide(&input).invalidate);
    input.event.message.generation = 2U;
    assert(exp3_blade_adapter_decide(&input).invalidate);
    input.event.message = (Exp3Message){0}; /* A session-reset Cancel always stops. */
    assert(exp3_blade_adapter_decide(&input).invalidate);
}

static void old_cancel_must_not_clear_a_new_ch4_request(void)
{
    BladeManualControl_t blade;
    BladeManualControlInput_t input = {0};
    Exp3BladeInput bridge = {0};
    Exp3BladeDecision decision;
    BladeConfirmOK_t ok = {0};
    BladeManualControl_Init(&blade, NULL);
    input.link_fresh = true;
    input.remote_run_enable = true;
    input.run_permitted = true;
    input.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
    input.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
    input.ch7_raw = 2000U;
    input.ch4_raw = 1500U;
    input.frame_sequence = 1U;
    BladeManualControl_Step(&blade, &input);
    input.ch4_raw = 988U;
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    input.ch4_raw = 1500U; /* Physically stops the old request immediately. */
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    ++input.frame_sequence; /* Let STOPPING settle before the next start edge. */
    BladeManualControl_Step(&blade, &input);
    input.ch4_raw = 988U;
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetState(&blade) == BLADE_STATE_START_CONFIRM_PENDING);
    bridge.event = confirm_event();
    bridge.event.kind = EXP3_PWM_CANCEL;
    bridge.event.message.kind = EXP3_RESULT;
    bridge.event.message.command = EXP3_CANCEL;
    bridge.event.message.status = EXP3_CANCELLED;
    bridge.ch4_epoch = 2U;
    bridge.blade_state = BladeManualControl_GetState(&blade);
    bridge.ch4_raw = input.ch4_raw;
    bridge.request_sequence = BladeManualControl_GetRequestSequence(&blade);
    bridge.gate_permitted = true;
    decision = exp3_blade_adapter_decide(&bridge);
    input.command_invalidated = decision.invalidate;
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetState(&blade) == BLADE_STATE_START_CONFIRM_PENDING);
    bridge.event = confirm_event();
    bridge.event.message.generation = 2U;
    bridge.event.ch4_epoch = 2U;
    bridge.blade_state = BladeManualControl_GetState(&blade);
    decision = exp3_blade_adapter_decide(&bridge);
    assert(decision.inject_confirm);
    ok.version = BLADE_PROTOCOL_VERSION;
    ok.request_sequence = decision.request_sequence;
    ok.event_sequence = decision.event_sequence;
    BladeManualControl_InjectConfirmOK(&blade, &ok);
    input.command_invalidated = false;
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    ++input.frame_sequence;
    BladeManualControl_Step(&blade, &input);
    assert(BladeManualControl_GetCommand(&blade) == 45.0f);
}

int main(void)
{
    accepted_result_crosses_existing_blade_state_machine_once();
    denied_or_cancelled_results_fail_closed();
    old_cancel_must_not_clear_a_new_ch4_request();
    puts("PASS Experiment3-to-blade gate and CH7 PWM low/medium/high commands");
    return 0;
}
