#include "blade_manual_control.h"

#include <string.h>

BladeManualControlConfig_t BladeManualControl_DefaultConfig(void)
{
    BladeManualControlConfig_t config;
    memset(&config, 0, sizeof(config));

    config.ch4_center_us = REMOTE_BLADE_START_CENTER_US;
    config.ch4_start_us = REMOTE_BLADE_START_START_US;
    config.ch4_tolerance_us = REMOTE_BLADE_START_TOLERANCE_US;

    config.ch7_high_us = REMOTE_IBUS_CAL_MIN_US;
    config.ch7_medium_us = REMOTE_IBUS_CAL_CENTER_US;
    config.ch7_low_us = REMOTE_IBUS_CAL_MAX_US;
    config.ch7_tolerance_us = REMOTE_SWITCH_POSITION_TOLERANCE_US;

#if BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_PWM
    config.output_min = BLADE_PWM_DUTY_MIN_PERCENT;
    config.output_medium = BLADE_PWM_DUTY_LEVEL_2_PERCENT;
    config.output_max = BLADE_PWM_DUTY_MAX_PERCENT;
    config.output_deadzone = BLADE_PWM_DUTY_DEADZONE_PERCENT;
#elif BLADE_MOTOR_DRIVER_USED == BLADE_MOTOR_DRIVER_FTESC_CAN
    config.output_min = BLADE_FTESC_ERPM_LEVEL_1;
    config.output_medium = BLADE_FTESC_ERPM_LEVEL_2;
    config.output_max = BLADE_FTESC_ERPM_LEVEL_3;
    config.output_deadzone = 0.0f;
#else
    config.output_min = 0.0f;
    config.output_medium = 0.0f;
    config.output_max = 0.0f;
    config.output_deadzone = 0.0f;
#endif

    return config;
}

void BladeManualControl_Init(BladeManualControl_t *ctrl,
                             const BladeManualControlConfig_t *config)
{
    if (ctrl == NULL)
    {
        return;
    }

    memset(ctrl, 0, sizeof(*ctrl));
    if (config != NULL)
    {
        ctrl->config = *config;
    }
    else
    {
        ctrl->config = BladeManualControl_DefaultConfig();
    }

    ctrl->state = BLADE_STATE_IDLE;
    ctrl->last_ch4_position = BLADE_CH4_POS_UNKNOWN;
    ctrl->request_output.version = BLADE_PROTOCOL_VERSION;
    ctrl->request_output.state = BLADE_STATE_IDLE;
}

void BladeManualControl_Reset(BladeManualControl_t *ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    BladeManualControlConfig_t cfg = ctrl->config;
    BladeManualControl_Init(ctrl, &cfg);
}

BladeCh4Position_t BladeManualControl_ClassifyCh4(const BladeManualControlConfig_t *config,
                                                  uint16_t pulse_us)
{
    if (config == NULL || config->ch4_center_us == 0U)
    {
        return BLADE_CH4_POS_UNKNOWN;
    }

    uint16_t tol = config->ch4_tolerance_us;
    if ((pulse_us >= (config->ch4_center_us - tol)) &&
        (pulse_us <= (config->ch4_center_us + tol)))
    {
        return BLADE_CH4_POS_CENTER;
    }

    if (config->ch4_start_us > config->ch4_center_us)
    {
        if (pulse_us >= (config->ch4_start_us - tol))
        {
            return BLADE_CH4_POS_START;
        }
    }
    else if (config->ch4_start_us < config->ch4_center_us)
    {
        if (pulse_us <= (config->ch4_start_us + tol))
        {
            return BLADE_CH4_POS_START;
        }
    }

    return BLADE_CH4_POS_OTHER;
}

static bool BladeManualControl_Near(uint16_t value, uint16_t expected,
                                    uint16_t tolerance)
{
    uint16_t difference = value > expected ? value - expected : expected - value;
    return difference <= tolerance;
}

float BladeManualControl_MapSpeed(const BladeManualControlConfig_t *config,
                                  uint16_t ch7_us)
{
    if (config == NULL)
    {
        return 0.0f;
    }

    if (BladeManualControl_Near(ch7_us, config->ch7_low_us,
                                config->ch7_tolerance_us))
    {
        return config->output_min;
    }
    if (BladeManualControl_Near(ch7_us, config->ch7_medium_us,
                                config->ch7_tolerance_us))
    {
        return config->output_medium;
    }
    if (BladeManualControl_Near(ch7_us, config->ch7_high_us,
                                config->ch7_tolerance_us))
    {
        return config->output_max;
    }
    return 0.0f;
}

BladeManualState_t BladeManualControl_GetState(const BladeManualControl_t *ctrl)
{
    return ctrl != NULL ? ctrl->state : BLADE_STATE_IDLE;
}

float BladeManualControl_GetCommand(const BladeManualControl_t *ctrl)
{
    return ctrl != NULL ? ctrl->commanded_speed : 0.0f;
}

bool BladeManualControl_GetStartConfirmRequest(const BladeManualControl_t *ctrl,
                                              BladeStartConfirmRequest_t *request)
{
    if (ctrl == NULL || request == NULL)
    {
        return false;
    }
    *request = ctrl->request_output;
    return (ctrl->state == BLADE_STATE_START_CONFIRM_PENDING);
}

uint32_t BladeManualControl_GetRequestSequence(const BladeManualControl_t *ctrl)
{
    return ctrl != NULL ? ctrl->request_sequence : 0U;
}

bool BladeManualControl_IsCenterBaselineObserved(const BladeManualControl_t *ctrl)
{
    return ctrl != NULL ? ctrl->center_baseline_observed : false;
}

void BladeManualControl_InjectConfirmOK(BladeManualControl_t *ctrl,
                                       const BladeConfirmOK_t *confirm_event)
{
    if (ctrl == NULL || confirm_event == NULL)
    {
        return;
    }
    ctrl->injected_event = *confirm_event;
    ctrl->has_injected_event = true;
}

void BladeManualControl_Step(BladeManualControl_t *ctrl,
                             const BladeManualControlInput_t *input)
{
    if (ctrl == NULL || input == NULL)
    {
        return;
    }

    /* 1. Classify CH4 position */
    BladeCh4Position_t ch4_pos;
    if (input->ch4_position != BLADE_CH4_POS_UNKNOWN)
    {
        ch4_pos = input->ch4_position;
    }
    else if (ctrl->config.ch4_center_us != 0U)
    {
        ch4_pos = BladeManualControl_ClassifyCh4(&ctrl->config, input->ch4_raw);
    }
    else
    {
        ch4_pos = BLADE_CH4_POS_OTHER;
    }

    /* 2. Evaluate all 9 immediate safety stop conditions */
    bool link_timeout = !input->link_fresh || input->timeout_latched;
    bool run_enable_stopped = input->run_enable_changed || !input->remote_run_enable || !input->run_permitted;
    bool physical_latched = input->physical_latched || input->emergency_active ||
                            input->collision_active || input->tilt_active;
    bool software_locked = input->software_locked;
    bool remote_latched = input->remote_recoverable_latched;
    bool owner_invalid = input->owner_changed || (input->selected_owner != EXECUTION_OWNER_MANUAL_REMOTE);
    bool command_invalidated = input->command_invalidated ||
                               (ctrl->initialized &&
                                (input->command_invalidation_sequence != ctrl->last_invalidation_sequence));
    bool gate_stop = input->gate_stop_requested;
    bool gear_invalid = BladeManualControl_MapSpeed(&ctrl->config, input->ch7_raw) <= 0.0f;
    bool ch4_center = (ch4_pos == BLADE_CH4_POS_CENTER);

    bool any_safety_stop = link_timeout || run_enable_stopped || physical_latched ||
                           software_locked || remote_latched || owner_invalid ||
                           command_invalidated || gate_stop || gear_invalid;

    /* Any safety stop invalidates both the observed center and the one-shot
     * start arm. Recovery requires a newly observed CENTER sample. */
    if (any_safety_stop)
    {
        ctrl->center_baseline_observed = false;
        ctrl->start_armed = false;
    }

    /* Fetch confirmation event if available */
    bool has_event = false;
    BladeConfirmOK_t ok;
    memset(&ok, 0, sizeof(ok));
    if (input->has_confirm_event)
    {
        has_event = true;
        ok = input->confirm_event;
    }
    else if (ctrl->has_injected_event)
    {
        has_event = true;
        ok = ctrl->injected_event;
        ctrl->has_injected_event = false;
    }

    /* 3. State Machine transitions */
    switch (ctrl->state)
    {
    case BLADE_STATE_STOPPING:
        ctrl->state = BLADE_STATE_IDLE;
        ctrl->commanded_speed = 0.0f;
        ctrl->request_active = false;
        ctrl->fresh_gear_received = false;
        ctrl->start_armed = false;
        if (!any_safety_stop && (ch4_pos == BLADE_CH4_POS_CENTER))
        {
            ctrl->center_baseline_observed = true;
            ctrl->start_armed = true;
        }
        break;

    case BLADE_STATE_RUNNING:
        if (any_safety_stop || ch4_center || (ch4_pos != BLADE_CH4_POS_START))
        {
            ctrl->state = BLADE_STATE_STOPPING;
            ctrl->commanded_speed = 0.0f;
            ctrl->request_active = false;
            ctrl->fresh_gear_received = false;
            ctrl->start_armed = false;
            if (ch4_pos == BLADE_CH4_POS_CENTER)
            {
                ctrl->center_baseline_observed = true;
                ctrl->start_armed = true;
            }
            else
            {
                ctrl->center_baseline_observed = false;
            }
        }
        else
        {
            if (input->frame_sequence != ctrl->confirmed_frame_sequence)
            {
                ctrl->fresh_gear_received = true;
            }

            if (ctrl->fresh_gear_received)
            {
                if (input->blade_speed_override > 0.0f)
                {
                    float spd = input->blade_speed_override;
                    if (spd < ctrl->config.output_min)
                    {
                        spd = ctrl->config.output_min;
                    }
                    if (spd > ctrl->config.output_max)
                    {
                        spd = ctrl->config.output_max;
                    }
                    ctrl->commanded_speed = spd;
                }
                else
                {
                    uint16_t confirmed_raw = 0U;
                    if (input->confirmed_gear == REMOTE_BLADE_GEAR_LOW)
                        confirmed_raw = ctrl->config.ch7_low_us;
                    else if (input->confirmed_gear == REMOTE_BLADE_GEAR_MEDIUM)
                        confirmed_raw = ctrl->config.ch7_medium_us;
                    else if (input->confirmed_gear == REMOTE_BLADE_GEAR_HIGH)
                        confirmed_raw = ctrl->config.ch7_high_us;
                    ctrl->commanded_speed = BladeManualControl_MapSpeed(&ctrl->config, confirmed_raw);
                }
            }
            else
            {
                ctrl->commanded_speed = 0.0f;
            }
        }
        break;

    case BLADE_STATE_START_CONFIRM_PENDING:
        if (any_safety_stop || ch4_center || (ch4_pos != BLADE_CH4_POS_START))
        {
            ctrl->state = BLADE_STATE_STOPPING;
            ctrl->commanded_speed = 0.0f;
            ctrl->request_active = false;
            ctrl->fresh_gear_received = false;
            ctrl->start_armed = false;
            if (ch4_pos == BLADE_CH4_POS_CENTER)
            {
                ctrl->center_baseline_observed = true;
                ctrl->start_armed = true;
            }
            else
            {
                ctrl->center_baseline_observed = false;
            }
        }
        else
        {
            ctrl->commanded_speed = 0.0f;

            if (has_event)
            {
                if (BladeHmi_IsConfirmOKValid(&ok, ctrl->request_sequence,
                                              ctrl->baseline_event_sequence,
                                              ctrl->last_consumed_event_sequence))
                {
                    ctrl->last_consumed_event_sequence = ok.event_sequence;
                    ctrl->state = BLADE_STATE_RUNNING;
                    ctrl->request_active = false;
                    ctrl->confirmed_frame_sequence = input->frame_sequence;
                    ctrl->fresh_gear_received = false;
                    ctrl->commanded_speed = 0.0f;
                }
            }
        }
        break;

    case BLADE_STATE_IDLE:
    default:
        ctrl->commanded_speed = 0.0f;
        ctrl->request_active = false;
        ctrl->fresh_gear_received = false;

        if (!any_safety_stop && (ch4_pos == BLADE_CH4_POS_CENTER))
        {
            ctrl->center_baseline_observed = true;
            ctrl->start_armed = true;
        }

        if (!any_safety_stop &&
            ctrl->start_armed &&
            (ch4_pos == BLADE_CH4_POS_START))
        {
            ctrl->request_sequence++;
            if (ctrl->request_sequence == 0U)
            {
                ctrl->request_sequence = 1U;
            }

            if (input->confirm_event_sequence != 0U)
            {
                ctrl->baseline_event_sequence = input->confirm_event_sequence;
            }
            else if (has_event)
            {
                ctrl->baseline_event_sequence = ok.event_sequence;
            }
            else
            {
                ctrl->baseline_event_sequence = ctrl->last_consumed_event_sequence;
            }

            ctrl->state = BLADE_STATE_START_CONFIRM_PENDING;
            ctrl->request_active = true;
            ctrl->fresh_gear_received = false;
            ctrl->commanded_speed = 0.0f;
            ctrl->start_armed = false;
        }
        break;
    }

    /* 4. Update request output */
    ctrl->request_output.version = BLADE_PROTOCOL_VERSION;
    ctrl->request_output.state = ctrl->state;
    ctrl->request_output.request_sequence =
        (ctrl->state == BLADE_STATE_START_CONFIRM_PENDING) ? ctrl->request_sequence : 0U;

    /* 5. Bookkeeping */
    ctrl->last_ch4_position = ch4_pos;
    ctrl->last_invalidation_sequence = input->command_invalidation_sequence;
    ctrl->last_owner = input->selected_owner;
    ctrl->last_run_enable = input->remote_run_enable;
    ctrl->initialized = true;
}
