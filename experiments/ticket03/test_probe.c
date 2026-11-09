#include "exp3_probe.h"

#include <assert.h>
#include <stdio.h>

static void ch4_requires_center_baseline_before_prompt(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;

    exp3_ui_probe_init(&probe);
    input.ch4_us = 988U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_IDLE);

    input.ch4_us = 1500U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_IDLE);

    input.ch4_us = 988U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_PROMPT);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);
}

static Exp3UiInput start_input(void)
{
    Exp3UiInput input = {0};
    input.ch4_us = 988U;
    input.bound = 1U;
    input.now_ms = 100U;
    return input;
}

static void ok_submits_confirm_and_matching_result_is_accepted(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;

    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 0x11223344U, 0x55667788U, 9U, 2U, 3U);
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input = start_input();
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_PROMPT);

    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_WAIT);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    assert(output.message.kind == EXP3_CONFIRM);
    assert(output.message.controller == 0x11223344U);
    assert(output.message.hmi == 0x55667788U);
    assert(output.message.request == 1U && output.message.action == 1U);
    assert(output.message.context == 9U && output.message.target == 2U);
    assert(output.message.generation == 3U);

    input.ok_pressed = 0U;
    input.matches = 1U;
    input.last_result = output.message;
    input.last_result.kind = EXP3_RESULT;
    input.last_result.sender = 2U;
    input.last_result.status = EXP3_ACCEPTED;
    input.last_result.command = EXP3_CONFIRM;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_ACCEPTED);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);
}

static void accepted_result_requires_center_then_allows_new_prompt(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput first, output;

    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    input.bound = 1U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_PROMPT);
    input.ok_pressed = 1U;
    first = exp3_ui_probe_step(&probe, &input);
    assert(first.submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    input.ok_pressed = 0U;
    input.matches = 1U;
    input.last_result = first.message;
    input.last_result.kind = EXP3_RESULT;
    input.last_result.sender = 2U;
    input.last_result.status = EXP3_ACCEPTED;
    input.last_result.command = EXP3_CONFIRM;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_ACCEPTED);
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_ACCEPTED);

    input.ch4_us = 1500U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_CANCEL_WAIT);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CANCEL);
    assert(output.message.action == first.message.action);
    input.ch4_us = 988U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_PROMPT);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);
}

static void unbound_query_and_cancel_do_not_create_confirmation(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;

    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, 3U, 4U, 5U);
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_UNBOUND);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);
    input.ok_pressed = 0U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_UNBOUND);
}

static void leaving_start_queues_cancel_after_confirm_transport_finishes(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput confirm, output;

    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, 3U, 4U, 5U);
    input.ch4_us = 1500U;
    input.bound = 1U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    confirm = exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 0U;
    input.ch4_us = 1500U;
    input.transport_waiting = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state == EXP3_UI_CANCEL_WAIT);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);

    input.transport_waiting = 0U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CANCEL);
    assert(output.message.kind == EXP3_CANCEL);
    assert(output.message.action == confirm.message.action);
}

static void timeout_still_tombstones_the_unresolved_identity(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;

    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    input.bound = 1U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    input.ok_pressed = 0U;
    input.now_ms = 60000U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_TIMEOUT);
    input.ch4_us = 1500U;
    input.transport_waiting = 0U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CANCEL);
    assert(output.message.action == 1U);
}

static void abandoned_prompt_advances_ch4_generation(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;
    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    input.bound = 1U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_PROMPT);
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    assert(output.message.generation == 2U);
}

static void pending_cancel_blocks_new_confirm(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;
    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    input.bound = 1U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    assert(exp3_ui_probe_step(&probe, &input).submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    input.ok_pressed = 0U;
    input.ch4_us = 1500U;
    input.transport_waiting = 1U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_NONE);
    assert(output.state != EXP3_UI_PROMPT);
    input.transport_waiting = 0U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CANCEL);
    assert(output.message.action == 1U);
}

static void cancel_reply_wait_must_not_offer_an_inactive_ok(void)
{
    Exp3UiProbe probe;
    Exp3UiInput input = {0};
    Exp3UiOutput output;
    exp3_ui_probe_init(&probe);
    exp3_ui_probe_bind(&probe, 1U, 2U, EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    input.bound = 1U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    input.last_result = output.message;
    input.last_result.kind = EXP3_RESULT;
    input.last_result.sender = 2U;
    input.last_result.command = EXP3_CONFIRM;
    input.last_result.status = EXP3_ACCEPTED;
    input.matches = 1U;
    input.ok_pressed = 0U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_ACCEPTED);
    input.ch4_us = 1500U;
    assert(exp3_ui_probe_step(&probe, &input).submit_kind == EXP3_UI_SUBMIT_CANCEL);
    input.transport_waiting = 1U; /* Cancel was submitted; slow reply is in flight. */
    input.ch4_us = 988U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.state != EXP3_UI_PROMPT);
    input.ok_pressed = 1U;
    assert(exp3_ui_probe_step(&probe, &input).submit_kind == EXP3_UI_SUBMIT_NONE);
    input.ok_pressed = 0U;
    input.ch4_us = 1500U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.ch4_us = 988U;
    (void)exp3_ui_probe_step(&probe, &input);
    input.transport_waiting = 0U;
    assert(exp3_ui_probe_step(&probe, &input).state == EXP3_UI_PROMPT);
    input.ok_pressed = 1U;
    output = exp3_ui_probe_step(&probe, &input);
    assert(output.submit_kind == EXP3_UI_SUBMIT_CONFIRM);
    assert(output.message.generation == 3U);
}

static Exp3Message result_message(uint8_t command, uint8_t status)
{
    Exp3Message result = {0};
    result.kind = EXP3_RESULT;
    result.sender = 2U;
    result.controller = 1U;
    result.hmi = 2U;
    result.request = 3U;
    result.action = 4U;
    result.context = 5U;
    result.generation = 6U;
    result.target = 7U;
    result.command = command;
    result.status = status;
    return result;
}

static void controller_emits_one_event_per_result_identity(void)
{
    Exp3PwmProbe probe;
    Exp3Message result = result_message(EXP3_CONFIRM, EXP3_ACCEPTED);
    Exp3PwmEvent event;

    exp3_pwm_probe_init(&probe);
    event = exp3_pwm_probe_observe(&probe, &result);
    assert(event.kind == EXP3_PWM_CONFIRM);
    assert(event.message.action == 4U);
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_NONE);

    result.command = EXP3_QUERY;
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_NONE);
    result.command = EXP3_CONFIRM;
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_NONE);

    result.command = EXP3_QUERY;
    result.action++;
    result.request++;
    result.status = EXP3_UNKNOWN;
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_NONE);

    result.command = EXP3_CANCEL;
    result.status = EXP3_CANCELLED;
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_CANCEL);
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_NONE);
}

static void evicted_result_cannot_reauthorize(void)
{
    Exp3PwmProbe probe;
    Exp3Message result = result_message(EXP3_CONFIRM, EXP3_ACCEPTED);
    Exp3Message old = result;
    unsigned i;
    exp3_pwm_probe_init(&probe);
    assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_CONFIRM);
    for (i = 0U; i < EXP3_CACHE_SIZE + 1U; ++i) {
        ++result.action;
        ++result.request;
        ++result.generation;
        assert(exp3_pwm_probe_observe(&probe, &result).kind == EXP3_PWM_CONFIRM);
    }
    assert(exp3_pwm_probe_observe(&probe, &old).kind == EXP3_PWM_NONE);
}

int main(void)
{
    ch4_requires_center_baseline_before_prompt();
    ok_submits_confirm_and_matching_result_is_accepted();
    accepted_result_requires_center_then_allows_new_prompt();
    unbound_query_and_cancel_do_not_create_confirmation();
    leaving_start_queues_cancel_after_confirm_transport_finishes();
    timeout_still_tombstones_the_unresolved_identity();
    abandoned_prompt_advances_ch4_generation();
    pending_cancel_blocks_new_confirm();
    cancel_reply_wait_must_not_offer_an_inactive_ok();
    controller_emits_one_event_per_result_identity();
    evicted_result_cannot_reauthorize();
    puts("PASS CH4 center baseline before prompt");
    return 0;
}
