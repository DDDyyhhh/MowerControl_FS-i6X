#include "exp3_blade_adapter.h"

#if EXP3_ENABLED
#include <string.h>

Exp3BladeDecision exp3_blade_adapter_decide(const Exp3BladeInput *input)
{
    Exp3BladeDecision decision;
    memset(&decision, 0, sizeof(decision));
    if (!input)
        return decision;
    if (input->event.kind == EXP3_PWM_CANCEL) {
        /* A session reset has no message identity and always stops the blade.
         * A delayed wire Cancel belongs only to its original CH4 edge. */
        decision.invalidate = input->event.message.kind == 0U ||
            (input->event.message.context == EXP3_PWM_CONTEXT &&
             input->event.message.target == EXP3_PWM_TARGET &&
             input->event.message.generation == input->ch4_epoch);
        return decision;
    }
    if (input->event.kind == EXP3_PWM_CONFIRM) {
        if (!input->gate_permitted)
            decision.reason = EXP3_BLADE_DENY_GATE;
        else if (input->blade_state != BLADE_STATE_START_CONFIRM_PENDING)
            decision.reason = EXP3_BLADE_DENY_STATE;
        else if (!input->request_sequence)
            decision.reason = EXP3_BLADE_DENY_REQUEST;
        else if (input->ch4_raw < 900U || input->ch4_raw > 1100U)
            decision.reason = EXP3_BLADE_DENY_CH4;
        else if (input->event.message.context != EXP3_PWM_CONTEXT ||
                 input->event.message.target != EXP3_PWM_TARGET)
            decision.reason = EXP3_BLADE_DENY_TARGET;
        else if (!input->ch4_epoch || input->event.message.generation != input->ch4_epoch)
            decision.reason = EXP3_BLADE_DENY_GENERATION;
        if (decision.reason != EXP3_BLADE_ALLOW)
            return decision;
        decision.inject_confirm = true;
        decision.request_sequence = input->request_sequence;
        decision.event_sequence = input->baseline_event_sequence;
        if ((int32_t)(input->last_consumed_event_sequence - decision.event_sequence) > 0)
            decision.event_sequence = input->last_consumed_event_sequence;
        ++decision.event_sequence;
        if (decision.event_sequence == 0U) {
            decision.inject_confirm = false;
            decision.reason = EXP3_BLADE_DENY_SEQUENCE;
        }
    }
    return decision;
}
#endif
