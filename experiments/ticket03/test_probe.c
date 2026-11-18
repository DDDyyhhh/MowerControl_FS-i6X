#include "exp3_probe.h"

#include <assert.h>
#include <stdio.h>

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
    controller_emits_one_event_per_result_identity();
    evicted_result_cannot_reauthorize();
    puts("PASS controller PWM probe emits one event per result identity");
    return 0;
}
