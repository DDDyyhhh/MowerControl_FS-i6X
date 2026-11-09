#include "blade_confirm_ch6_adapter.h"

#include <string.h>

BladeConfirmCh6Config_t BladeConfirmCh6Adapter_DefaultConfig(void)
{
    BladeConfirmCh6Config_t config;
    memset(&config, 0, sizeof(config));
    config.low_us = REMOTE_BLADE_CONFIRM_LOW_US;
    config.high_us = REMOTE_BLADE_CONFIRM_HIGH_US;
    config.tolerance_us = REMOTE_BLADE_CONFIRM_TOLERANCE_US;
    config.confirm_frames = REMOTE_BLADE_CONFIRM_FRAMES;
    config.semantics_configured = true;
    return config;
}

void BladeConfirmCh6Adapter_Init(BladeConfirmCh6Adapter_t *adapter,
                                 const BladeConfirmCh6Config_t *config)
{
    if (adapter == NULL)
    {
        return;
    }

    memset(adapter, 0, sizeof(*adapter));
    if (config != NULL)
    {
        adapter->config = *config;
    }
    else
    {
        adapter->config = BladeConfirmCh6Adapter_DefaultConfig();
    }

    adapter->current_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
    adapter->candidate_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
    adapter->baseline_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
    adapter->initialized = true;
}

void BladeConfirmCh6Adapter_Reset(BladeConfirmCh6Adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }
    BladeConfirmCh6Config_t cfg = adapter->config;
    BladeConfirmCh6Adapter_Init(adapter, &cfg);
}

BladeConfirmCh6Position_t BladeConfirmCh6Adapter_ClassifyCh6(
    const BladeConfirmCh6Config_t *config, uint16_t pulse_us)
{
    if (config == NULL || !config->semantics_configured)
    {
        return BLADE_CONFIRM_CH6_POS_UNKNOWN;
    }

    uint16_t tol = config->tolerance_us;
    uint16_t low_min = (config->low_us > tol) ? (uint16_t)(config->low_us - tol) : 0U;
    uint16_t low_max = (uint16_t)(config->low_us + tol);
    if ((pulse_us >= low_min) && (pulse_us <= low_max))
    {
        return BLADE_CONFIRM_CH6_POS_LOW;
    }

    uint16_t high_min = (config->high_us > tol) ? (uint16_t)(config->high_us - tol) : 0U;
    uint16_t high_max = (uint16_t)(config->high_us + tol);
    if ((pulse_us >= high_min) && (pulse_us <= high_max))
    {
        return BLADE_CONFIRM_CH6_POS_HIGH;
    }

    return BLADE_CONFIRM_CH6_POS_UNKNOWN;
}

void BladeConfirmCh6Adapter_Advance(BladeConfirmCh6Adapter_t *adapter,
                                    const BladeConfirmCh6Input_t *input)
{
    if (adapter == NULL || input == NULL)
    {
        return;
    }

    /* 1. If link is offline or timed out, invalidate baseline and clear event */
    if (!input->link_fresh || input->timeout_latched)
    {
        adapter->baseline_observed = false;
        adapter->current_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
        adapter->candidate_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
        adapter->candidate_count = 0U;
        adapter->event_available = false;
        adapter->last_link_fresh = false;
        adapter->last_timeout_latched = true;
        return;
    }

    /* 2. Check for link recovery */
    if (!adapter->last_link_fresh || adapter->last_timeout_latched)
    {
        adapter->baseline_observed = false;
        adapter->current_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
        adapter->candidate_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
        adapter->candidate_count = 0U;
        adapter->event_available = false;
    }
    adapter->last_link_fresh = true;
    adapter->last_timeout_latched = false;

    /* 3. Ignore duplicate queries on the same frame sequence */
    if (adapter->initialized &&
        (input->frame_sequence != 0U) &&
        (input->frame_sequence == adapter->last_frame_sequence))
    {
        return;
    }
    adapter->last_frame_sequence = input->frame_sequence;
    /* 3. If semantics not configured, no event generation */
    if (!adapter->config.semantics_configured)
    {
        adapter->event_available = false;
        return;
    }

    /* 4. Classify position */
    BladeConfirmCh6Position_t pos =
        BladeConfirmCh6Adapter_ClassifyCh6(&adapter->config, input->ch6_raw);

    if (pos == BLADE_CONFIRM_CH6_POS_UNKNOWN)
    {
        /* Noise resets only the debounce candidate. A generated event remains
         * latched until its explicit consumer acknowledges it. */
        adapter->candidate_position = BLADE_CONFIRM_CH6_POS_UNKNOWN;
        adapter->candidate_count = 0U;
        return;
    }

    /* 5. Debounce candidate */
    if (pos != adapter->candidate_position)
    {
        adapter->candidate_position = pos;
        adapter->candidate_count = 1U;
    }
    else if (adapter->candidate_count < adapter->config.confirm_frames)
    {
        adapter->candidate_count++;
    }

    /* 6. Check confirmation threshold */
    if (adapter->candidate_count >= adapter->config.confirm_frames)
    {
        if (!adapter->baseline_observed)
        {
            /* First observed stable position establishes baseline ONLY! */
            adapter->baseline_observed = true;
            adapter->baseline_position = pos;
            adapter->current_position = pos;
            adapter->event_available = false;
        }
        else if (pos != adapter->current_position)
        {
            /* One stable physical edge advances the stable position once.
             * A newer edge supersedes an older unconsumed event; holding the
             * same stable position never regenerates or clears the event. */
            adapter->current_position = pos;
            adapter->event_sequence++;
            if (adapter->event_sequence == 0U)
            {
                adapter->event_sequence = 1U; /* 0 reserved for none */
            }
            adapter->event_available = true;
            adapter->event_consumed = false;
            adapter->latest_event.version = BLADE_PROTOCOL_VERSION;
            adapter->latest_event.request_sequence = input->active_request_sequence;
            adapter->latest_event.event_sequence = adapter->event_sequence;
        }
        else
        {
            /* Stable position unchanged. Preserve any unconsumed event. */
        }
    }
    else
    {
        /* Debounce is incomplete. Preserve any unconsumed event. */
    }
}

bool BladeConfirmCh6Adapter_IsBaselineObserved(const BladeConfirmCh6Adapter_t *adapter)
{
    return (adapter != NULL) ? adapter->baseline_observed : false;
}

BladeConfirmCh6Position_t BladeConfirmCh6Adapter_GetCurrentPosition(const BladeConfirmCh6Adapter_t *adapter)
{
    return (adapter != NULL) ? adapter->current_position : BLADE_CONFIRM_CH6_POS_UNKNOWN;
}

uint32_t BladeConfirmCh6Adapter_GetEventSequence(const BladeConfirmCh6Adapter_t *adapter)
{
    return (adapter != NULL) ? adapter->event_sequence : 0U;
}

bool BladeConfirmCh6Adapter_HasEvent(const BladeConfirmCh6Adapter_t *adapter)
{
    return (adapter != NULL) ? (adapter->event_available && !adapter->event_consumed) : false;
}

void BladeConfirmCh6Adapter_GetDiagnostics(const BladeConfirmCh6Adapter_t *adapter,
                                           BladeConfirmCh6Diagnostics_t *diagnostics)
{
    if ((adapter == NULL) || (diagnostics == NULL))
    {
        return;
    }
    diagnostics->raw = 0U;
    diagnostics->candidate_position = adapter->candidate_position;
    diagnostics->candidate_count = adapter->candidate_count;
    diagnostics->confirm_frames = adapter->config.confirm_frames;
    diagnostics->event_sequence = adapter->event_sequence;
    diagnostics->event_available = BladeConfirmCh6Adapter_HasEvent(adapter);
    diagnostics->event_request_sequence = adapter->latest_event.request_sequence;
}

bool BladeConfirmCh6Adapter_GetConfirmOK(BladeConfirmCh6Adapter_t *adapter,
                                        uint32_t expected_request_sequence,
                                        BladeConfirmOK_t *ok)
{
    if (adapter == NULL || ok == NULL)
    {
        return false;
    }
    if (!adapter->event_available || adapter->event_consumed)
    {
        return false;
    }
    if (expected_request_sequence == 0U)
    {
        return false;
    }
    if (adapter->latest_event.request_sequence != expected_request_sequence)
    {
        return false;
    }
    *ok = adapter->latest_event;
    return true;
}

void BladeConfirmCh6Adapter_ConsumeEvent(BladeConfirmCh6Adapter_t *adapter,
                                        uint32_t event_sequence)
{
    if (adapter == NULL)
    {
        return;
    }
    adapter->event_consumed = true;
    adapter->event_available = false;
    adapter->last_consumed_event_sequence = event_sequence;
}
