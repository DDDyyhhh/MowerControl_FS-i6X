#include "exp3_probe.h"

#if EXP3_ENABLED
#include <string.h>
#include "mower_control_contract.h"
#if MOWER_BLADE_OK_ENABLED
static uint32_t action_controller, action_hmi, next_action;
#endif

static uint8_t in_range(uint16_t value, uint16_t center, uint16_t tolerance)
{
    return value >= (uint16_t)(center - tolerance) &&
           value <= (uint16_t)(center + tolerance);
}

void exp3_ui_probe_init(Exp3UiProbe *probe)
{
    memset(probe, 0, sizeof(*probe));
}

void exp3_ui_probe_bind(Exp3UiProbe *probe, uint32_t controller, uint32_t hmi,
                        uint32_t context, uint16_t target, uint32_t generation)
{
#if MOWER_BLADE_OK_ENABLED
    if (controller!=action_controller || hmi!=action_hmi) {
        action_controller=controller; action_hmi=hmi; next_action=0U;
    }
#endif
    probe->controller = controller;
    probe->hmi = hmi;
    probe->context = context;
    probe->target = target;
    probe->next_generation = generation;
}

static uint8_t is_start(uint16_t ch4_us)
{
    return in_range(ch4_us, 1000U, 100U);
}

static void make_command(const Exp3UiProbe *probe, uint8_t kind, Exp3UiOutput *output)
{
    output->message = probe->pending;
    output->message.kind = kind;
    output->message.sender = 1U;
    output->message.status = 0U;
    output->message.command = 0U;
    output->submit_kind = kind == EXP3_CONFIRM ? EXP3_UI_SUBMIT_CONFIRM : EXP3_UI_SUBMIT_CANCEL;
}

static uint8_t matching_result(const Exp3Message *a, const Exp3Message *b)
{
    return b->kind == EXP3_RESULT && b->command == EXP3_CONFIRM &&
           a->controller == b->controller && a->hmi == b->hmi &&
           a->request == b->request && a->action == b->action &&
           a->context == b->context && a->generation == b->generation &&
           a->target == b->target;
}

Exp3UiOutput exp3_ui_probe_step(Exp3UiProbe *probe, const Exp3UiInput *input)
{
    Exp3UiOutput output;
    uint8_t start = is_start(input->ch4_us);
    memset(&output, 0, sizeof(output));

    /* Count physical center-to-start edges even while a Cancel is pending.
     * Prompt rendering must not be the source of the wire generation. */
    if (in_range(input->ch4_us, 1500U, 100U))
        probe->center_seen = 1U;
    if (start && !probe->ch4_start && probe->center_seen) {
        probe->prompt_generation = probe->next_generation++;
        if (probe->next_generation == 0U)
            ++probe->next_generation;
        probe->center_seen = 0U;
        probe->start_armed = 1U;
    }
    if (!start)
        probe->start_armed = 0U;
    probe->ch4_start = start;

    if (probe->cancel_pending) {
        probe->state = EXP3_UI_CANCEL_WAIT;
        if ((!input->transport_waiting || input->cancel_ready) && probe->pending.action != 0U) {
            make_command(probe, EXP3_CANCEL, &output);
            probe->cancel_pending = 0U;
        }
        output.state = probe->state;
        return output;
    }

    if (probe->state == EXP3_UI_CANCEL_WAIT) {
        if (input->transport_waiting) {
            output.state = probe->state;
            return output;
        }
        probe->state = EXP3_UI_IDLE;
    }

    if (!start) {
        if (probe->state == EXP3_UI_WAIT || probe->state == EXP3_UI_ACCEPTED ||
            probe->state == EXP3_UI_TIMEOUT)
            probe->cancel_pending = 1U;
        probe->state = EXP3_UI_IDLE;
        if (probe->cancel_pending)
            probe->state = EXP3_UI_CANCEL_WAIT;
        if (probe->cancel_pending && (!input->transport_waiting || input->cancel_ready) && probe->pending.action != 0U) {
            make_command(probe, EXP3_CANCEL, &output);
            probe->cancel_pending = 0U;
        }
    } else if (!probe->start_armed) {
        probe->state = EXP3_UI_IDLE;
    } else if (probe->state == EXP3_UI_IDLE) {
        probe->state = input->transport_waiting ? EXP3_UI_CANCEL_WAIT : EXP3_UI_PROMPT;
    } else if (probe->state == EXP3_UI_UNBOUND && input->bound) {
        probe->state = EXP3_UI_PROMPT;
    }

    if (probe->state == EXP3_UI_PROMPT && input->transport_waiting)
        probe->state = EXP3_UI_CANCEL_WAIT;

    if (probe->state == EXP3_UI_PROMPT && input->ok_pressed) {
        if (!input->bound) {
            probe->state = EXP3_UI_UNBOUND;
        } else if (!input->transport_waiting) {
            memset(&probe->pending, 0, sizeof(probe->pending));
            ++probe->next_action;
            if (probe->next_action == 0U)
                ++probe->next_action;
            probe->pending.controller = probe->controller;
            probe->pending.hmi = probe->hmi;
            probe->pending.request = probe->next_action;
#if MOWER_BLADE_OK_ENABLED
            if (next_action==UINT32_MAX) { probe->state=EXP3_UI_REJECTED; output.state=probe->state; return output; }
            probe->next_action=++next_action;
            probe->pending.request=probe->next_action;
#endif
            probe->pending.action = probe->next_action;
            probe->pending.context = probe->context;
            probe->pending.generation = probe->context==MOWER_CHASSIS_CONTEXT ? input->generation : probe->prompt_generation;
            probe->pending.target = probe->target;
            probe->sent_at = input->now_ms;
            probe->seen_matches = input->matches;
            probe->state = EXP3_UI_WAIT;
            make_command(probe, EXP3_CONFIRM, &output);
        }
    } else if (probe->state == EXP3_UI_WAIT && input->matches != probe->seen_matches) {
        probe->seen_matches = input->matches;
        if (matching_result(&probe->pending, &input->last_result))
            probe->state = input->last_result.status == EXP3_ACCEPTED ?
                           EXP3_UI_ACCEPTED : EXP3_UI_REJECTED;
    } else if (probe->state == EXP3_UI_WAIT &&
               (uint32_t)(input->now_ms - probe->sent_at) >= 60000U) {
        probe->state = EXP3_UI_TIMEOUT;
    }

    if (in_range(input->ch4_us, 1500U, 100U) && probe->state == EXP3_UI_IDLE) {
        probe->center_seen = 1U;
    }

    output.state = probe->state;
    return output;
}

void exp3_pwm_probe_init(Exp3PwmProbe *probe)
{
    memset(probe, 0, sizeof(*probe));
}

static uint8_t same_identity(const Exp3Message *a, const Exp3Message *b)
{
    return a->controller == b->controller && a->hmi == b->hmi &&
           a->request == b->request && a->action == b->action &&
           a->context == b->context && a->generation == b->generation &&
           a->target == b->target;
}

Exp3PwmEvent exp3_pwm_probe_observe(Exp3PwmProbe *probe, const Exp3Message *result)
{
    Exp3PwmEvent event;
    unsigned i;
    int slot = -1;
    uint8_t flag;
    memset(&event, 0, sizeof(event));
    if (!probe || !result || result->kind != EXP3_RESULT || result->sender != 2U)
        return event;
    if (!((result->command == EXP3_CONFIRM && result->status == EXP3_ACCEPTED) ||
          (result->command == EXP3_CANCEL && result->status == EXP3_CANCELLED)))
        return event;
    if (result->command == EXP3_CONFIRM &&
        (result->generation < probe->highest_generation ||
         (result->generation == probe->highest_generation &&
          result->action <= probe->highest_action)))
        return event;
    for (i = 0U; i < EXP3_CACHE_SIZE; ++i) {
        if (probe->flags[i] && same_identity(&probe->records[i], result)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        slot = probe->next;
        probe->next = (uint8_t)((probe->next + 1U) % EXP3_CACHE_SIZE);
        probe->records[slot] = *result;
        probe->flags[slot] = 0x80U;
    }
    flag = result->command == EXP3_CONFIRM ? 1U : 2U;
    if (probe->flags[slot] & flag)
        return event;
    probe->flags[slot] |= flag;
    if (result->command == EXP3_CONFIRM &&
        (result->generation > probe->highest_generation ||
         result->action > probe->highest_action)) {
        probe->highest_generation = result->generation;
        probe->highest_action = result->action;
    }
    event.message = *result;
    if (result->command == EXP3_CONFIRM && result->status == EXP3_ACCEPTED)
        event.kind = EXP3_PWM_CONFIRM;
    else if (result->command == EXP3_CANCEL && result->status == EXP3_CANCELLED)
        event.kind = EXP3_PWM_CANCEL;
    return event;
}
#endif
