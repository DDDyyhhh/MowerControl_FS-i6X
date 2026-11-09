#include "remote_authorization.h"

#include <string.h>

static bool RemoteAuthorization_ValueNear(uint16_t value,
                                          uint16_t expected,
                                          uint16_t tolerance)
{
    uint16_t difference = (value > expected) ? (uint16_t)(value - expected)
                                             : (uint16_t)(expected - value);
    return difference <= tolerance;
}

static RemoteRunEnableState_t RemoteAuthorization_DecodeRunEnable(
    const RemoteAuthorization_t *authorization,
    uint16_t value)
{
    if (!authorization->config.run_enable_semantics_configured)
    {
        return REMOTE_SWITCH_UNKNOWN;
    }
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.run_enable_off_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_SWITCH_OFF;
    }
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.run_enable_on_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_SWITCH_ON;
    }
    return REMOTE_SWITCH_UNKNOWN;
}

static RemoteUnlockState_t RemoteAuthorization_DecodeUnlock(
    const RemoteAuthorization_t *authorization,
    uint16_t value)
{
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.unlock_up_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_UNLOCK_UP;
    }
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.unlock_down_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_UNLOCK_DOWN;
    }
    return REMOTE_UNLOCK_UNKNOWN;
}
static RemoteBladeGear_t RemoteAuthorization_DecodeBladeGear(
    const RemoteAuthorization_t *authorization,
    uint16_t value)
{
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.blade_gear_low_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_BLADE_GEAR_LOW;
    }
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.blade_gear_medium_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_BLADE_GEAR_MEDIUM;
    }
    if (RemoteAuthorization_ValueNear(
            value, authorization->config.blade_gear_high_us,
            authorization->config.switch_position_tolerance_us))
    {
        return REMOTE_BLADE_GEAR_HIGH;
    }
    return REMOTE_BLADE_GEAR_UNKNOWN;
}


static bool RemoteAuthorization_UnlockChangeAllowed(
    const RemoteAuthorization_t *authorization,
    RemoteUnlockState_t before,
    RemoteUnlockState_t after)
{
    (void)authorization;
    return ((before == REMOTE_UNLOCK_DOWN) &&
            (after == REMOTE_UNLOCK_UP)) ||
           ((before == REMOTE_UNLOCK_UP) &&
            (after == REMOTE_UNLOCK_DOWN));
}

static bool RemoteAuthorization_SafetyAllowsUnlock(
    const RemoteAuthorizationInput_t *input)
{
    return !input->emergency_active &&
           !input->collision_active &&
           !input->tilt_active;
}

static bool RemoteAuthorization_SticksCentered(
    const RemoteAuthorization_t *authorization,
    const RemoteAuthorizationInput_t *input)
{
    return RemoteAuthorization_ValueNear(
               input->steering_raw,
               authorization->config.steering.center_us,
               authorization->config.stick_center_tolerance_us) &&
           RemoteAuthorization_ValueNear(
               input->throttle_raw,
               authorization->config.throttle.center_us,
               authorization->config.stick_center_tolerance_us);
}

static void RemoteAuthorization_Invalidate(
    RemoteAuthorization_t *authorization,
    RemoteRecoverableLatchReason_t reason)
{
    authorization->output.command_invalidation_sequence++;
    authorization->output.remote_recoverable_latched = true;
    authorization->output.run_permitted = false;
    authorization->output.latch_reason = reason;
    authorization->centered = false;
}

static void RemoteAuthorization_ResetAttempt(
    RemoteAuthorization_t *authorization)
{
    RemoteAuthorization_Invalidate(authorization,
                                   REMOTE_LATCH_REASON_SAFETY_REJECT);
    authorization->output.state = REMOTE_AUTH_STOPPED_LATCHED;
    authorization->unlock_started_ms = 0U;
}

static void RemoteAuthorization_ResetBaselines(
    RemoteAuthorization_t *authorization)
{
    authorization->run_baseline_valid = false;
    authorization->unlock_baseline_valid = false;
    authorization->run_candidate = REMOTE_SWITCH_UNKNOWN;
    authorization->unlock_candidate = REMOTE_UNLOCK_UNKNOWN;
    authorization->run_candidate_count = 0U;
    authorization->unlock_candidate_count = 0U;
    authorization->output.run_enable = REMOTE_SWITCH_UNKNOWN;
    authorization->output.unlock = REMOTE_UNLOCK_UNKNOWN;
}

static bool RemoteAuthorization_ConfirmRunEnable(
    RemoteAuthorization_t *authorization,
    RemoteRunEnableState_t sample,
    RemoteRunEnableState_t *before)
{
    if (sample == REMOTE_SWITCH_UNKNOWN)
    {
        authorization->run_candidate = REMOTE_SWITCH_UNKNOWN;
        authorization->run_candidate_count = 0U;
        return false;
    }
    if (sample != authorization->run_candidate)
    {
        authorization->run_candidate = sample;
        authorization->run_candidate_count = 1U;
    }
    else if (authorization->run_candidate_count < UINT8_MAX)
    {
        authorization->run_candidate_count++;
    }
    if ((authorization->run_candidate_count <
         authorization->config.switch_confirm_frames) ||
        (sample == authorization->output.run_enable))
    {
        return false;
    }
    *before = authorization->output.run_enable;
    authorization->output.run_enable = sample;
    return true;
}

static bool RemoteAuthorization_ConfirmUnlock(
    RemoteAuthorization_t *authorization,
    RemoteUnlockState_t sample,
    RemoteUnlockState_t *before)
{
    if (sample == REMOTE_UNLOCK_UNKNOWN)
    {
        authorization->unlock_candidate = REMOTE_UNLOCK_UNKNOWN;
        authorization->unlock_candidate_count = 0U;
        return false;
    }
    if (sample != authorization->unlock_candidate)
    {
        authorization->unlock_candidate = sample;
        authorization->unlock_candidate_count = 1U;
    }
    else if (authorization->unlock_candidate_count < UINT8_MAX)
    {
        authorization->unlock_candidate_count++;
    }
    if ((authorization->unlock_candidate_count <
         authorization->config.switch_confirm_frames) ||
        (sample == authorization->output.unlock))
    {
        return false;
    }
    *before = authorization->output.unlock;
    authorization->output.unlock = sample;
    return true;
}
static void RemoteAuthorization_ConfirmBladeGear(
    RemoteAuthorization_t *authorization,
    RemoteBladeGear_t sample)
{
    if (sample == REMOTE_BLADE_GEAR_UNKNOWN)
    {
        authorization->blade_gear_candidate = REMOTE_BLADE_GEAR_UNKNOWN;
        authorization->blade_gear_candidate_count = 0U;
        return;
    }
    if (sample != authorization->blade_gear_candidate)
    {
        authorization->blade_gear_candidate = sample;
        authorization->blade_gear_candidate_count = 1U;
    }
    else if (authorization->blade_gear_candidate_count < UINT8_MAX)
    {
        authorization->blade_gear_candidate_count++;
    }
    if (authorization->blade_gear_candidate_count >=
        authorization->config.switch_confirm_frames)
    {
        authorization->output.blade_gear = sample;
    }
}


static void RemoteAuthorization_HandleRunEnable(
    RemoteAuthorization_t *authorization,
    RemoteRunEnableState_t before,
    uint32_t now_ms)
{
    (void)before;
    (void)now_ms;

    if (!authorization->run_baseline_valid)
    {
        authorization->run_baseline_valid = true;
        authorization->output.state = REMOTE_AUTH_STOPPED_LATCHED;
        return;
    }

    RemoteAuthorization_Invalidate(authorization,
                                   REMOTE_LATCH_REASON_REMOTE_STOP);
    authorization->unlock_started_ms = 0U;
    authorization->output.state = REMOTE_AUTH_STOPPED_LATCHED;
}

static void RemoteAuthorization_HandleUnlock(
    RemoteAuthorization_t *authorization,
    RemoteUnlockState_t before,
    uint32_t now_ms)
{
    bool allowed_change;

    if (!authorization->unlock_baseline_valid)
    {
        authorization->unlock_baseline_valid = true;
        return;
    }
    allowed_change = RemoteAuthorization_UnlockChangeAllowed(
        authorization, before, authorization->output.unlock);
    if (!allowed_change ||
        (authorization->output.run_enable != REMOTE_SWITCH_ON))
    {
        return;
    }
    if (((authorization->output.state == REMOTE_AUTH_STOPPED_LATCHED) ||
         (authorization->output.state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_1)) &&
        (before == REMOTE_UNLOCK_DOWN) &&
        (authorization->output.unlock == REMOTE_UNLOCK_UP))
    {
        authorization->output.state = REMOTE_AUTH_WAIT_UNLOCK_EDGE_2;
        authorization->unlock_started_ms = now_ms;
    }
    else if ((authorization->output.state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2) &&
             (before == REMOTE_UNLOCK_UP) &&
             (authorization->output.unlock == REMOTE_UNLOCK_DOWN))
    {
        authorization->output.state = REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY;
        authorization->centered = true;
        authorization->center_started_ms = now_ms;
    }
}

void RemoteAuthorization_Init(RemoteAuthorization_t *authorization,
                              const FlySkyIbusConfig_t *config)
{
    if (authorization == NULL)
    {
        return;
    }
    memset(authorization, 0, sizeof(*authorization));
    if (config != NULL)
    {
        authorization->config = *config;
    }
    authorization->output.state = REMOTE_AUTH_SYNCHRONIZING;
    authorization->output.remote_recoverable_latched = true;
    authorization->output.latch_reason = REMOTE_LATCH_REASON_SYNCHRONIZING;
}

void RemoteAuthorization_Advance(RemoteAuthorization_t *authorization,
                                 const RemoteAuthorizationInput_t *input,
                                 uint32_t now_ms)
{
    bool new_frame;
    bool timeout_changed;
    RemoteRunEnableState_t run_before = REMOTE_SWITCH_UNKNOWN;
    RemoteUnlockState_t unlock_before = REMOTE_UNLOCK_UNKNOWN;
    RemoteRunEnableState_t run_sample;
    RemoteUnlockState_t unlock_sample;
    RemoteBladeGear_t blade_gear_sample;

    if ((authorization == NULL) || (input == NULL))
    {
        return;
    }

    timeout_changed = input->timeout_sequence !=
                      authorization->last_timeout_sequence;
    if (timeout_changed || !input->link_fresh || !input->stable_online)
    {
        if (timeout_changed ||
            (authorization->output.state != REMOTE_AUTH_SYNCHRONIZING))
        {
            RemoteAuthorization_Invalidate(authorization,
                timeout_changed ? REMOTE_LATCH_REASON_TIMEOUT
                                : REMOTE_LATCH_REASON_SYNCHRONIZING);
        }
        authorization->output.state = REMOTE_AUTH_SYNCHRONIZING;
        RemoteAuthorization_ResetBaselines(authorization);
        authorization->last_timeout_sequence = input->timeout_sequence;
        authorization->last_frame_sequence = input->frame_sequence;
        return;
    }

    if (!authorization->config.run_enable_semantics_configured)
    {
        authorization->output.state = REMOTE_AUTH_STOPPED_LATCHED;
        authorization->output.run_permitted = false;
        return;
    }

    if (authorization->output.state == REMOTE_AUTH_SYNCHRONIZING)
    {
        authorization->output.state = REMOTE_AUTH_STOPPED_LATCHED;
    }
    if (!RemoteAuthorization_SafetyAllowsUnlock(input) &&
        ((authorization->output.state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_1) ||
         (authorization->output.state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2) ||
         (authorization->output.state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY) ||
         (authorization->output.state == REMOTE_AUTH_MANUAL_ARMED)))
    {
        RemoteAuthorization_ResetAttempt(authorization);
    }

    new_frame = input->frame_sequence != authorization->last_frame_sequence;
    if (new_frame)
    {
        authorization->last_frame_sequence = input->frame_sequence;
        run_sample = RemoteAuthorization_DecodeRunEnable(authorization,
                                                         input->run_enable_raw);
        unlock_sample = RemoteAuthorization_DecodeUnlock(authorization,
                                                          input->unlock_raw);
        blade_gear_sample = RemoteAuthorization_DecodeBladeGear(
            authorization, input->blade_gear_raw);
        RemoteAuthorization_ConfirmBladeGear(authorization,
                                              blade_gear_sample);
        if (RemoteAuthorization_ConfirmRunEnable(authorization, run_sample,
                                                 &run_before))
        {
            RemoteAuthorization_HandleRunEnable(authorization, run_before,
                                                 now_ms);
        }
        if (RemoteAuthorization_ConfirmUnlock(authorization, unlock_sample,
                                              &unlock_before))
        {
            RemoteAuthorization_HandleUnlock(authorization, unlock_before,
                                              now_ms);
        }
    }

    if (((authorization->output.state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2) ||
         (authorization->output.state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY)) &&
        ((uint32_t)(now_ms - authorization->unlock_started_ms) >=
         authorization->config.unlock_window_ms))
    {
        RemoteAuthorization_ResetAttempt(authorization);
        return;
    }

    if (authorization->output.state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY)
    {
        if ((authorization->output.run_enable != REMOTE_SWITCH_ON) ||
            !RemoteAuthorization_SafetyAllowsUnlock(input))
        {
            RemoteAuthorization_ResetAttempt(authorization);
            return;
        }
        if (!RemoteAuthorization_SticksCentered(authorization, input))
        {
            authorization->centered = false;
            return;
        }
        if (!authorization->centered)
        {
            authorization->centered = true;
            authorization->center_started_ms = now_ms;
            return;
        }
        if ((uint32_t)(now_ms - authorization->center_started_ms) >=
            authorization->config.stick_center_hold_ms)
        {
            authorization->output.remote_recoverable_latched = false;
            authorization->output.latch_reason = REMOTE_LATCH_REASON_NONE;
            authorization->output.run_permitted = false;
            authorization->output.command_invalidation_sequence++;
            authorization->armed_frame_sequence = input->frame_sequence;
            authorization->output.state = REMOTE_AUTH_MANUAL_ARMED;
        }
    }
    else if ((authorization->output.state == REMOTE_AUTH_MANUAL_ARMED) &&
             !authorization->output.run_permitted &&
             (input->frame_sequence != authorization->armed_frame_sequence))
    {
        authorization->output.run_permitted = true;
    }
}

void RemoteAuthorization_GetOutput(const RemoteAuthorization_t *authorization,
                                   RemoteAuthorizationOutput_t *output)
{
    if ((authorization == NULL) || (output == NULL))
    {
        return;
    }
    *output = authorization->output;
}
