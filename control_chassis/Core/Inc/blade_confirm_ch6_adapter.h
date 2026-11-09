#ifndef BLADE_CONFIRM_CH6_ADAPTER_H
#define BLADE_CONFIRM_CH6_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "blade_hmi_contract.h"
#include "remote_backend_config.h"

typedef enum
{
    BLADE_CONFIRM_CH6_POS_UNKNOWN = 0,
    BLADE_CONFIRM_CH6_POS_LOW,
    BLADE_CONFIRM_CH6_POS_HIGH
} BladeConfirmCh6Position_t;

typedef struct
{
    uint16_t low_us;
    uint16_t high_us;
    uint16_t tolerance_us;
    uint8_t confirm_frames;
    bool semantics_configured;
} BladeConfirmCh6Config_t;

typedef struct
{
    uint16_t ch6_raw;
    bool link_fresh;
    bool timeout_latched;
    uint32_t frame_sequence;
    uint32_t active_request_sequence;
} BladeConfirmCh6Input_t;

typedef struct
{
    BladeConfirmCh6Config_t config;

    /* Baseline observation */
    bool baseline_observed;
    BladeConfirmCh6Position_t baseline_position;

    /* Current debounced state */
    BladeConfirmCh6Position_t current_position;
    BladeConfirmCh6Position_t candidate_position;
    uint8_t candidate_count;

    /* Event tracking */
    uint32_t event_sequence;
    bool event_available;
    bool event_consumed;
    uint32_t last_consumed_event_sequence;
    BladeConfirmOK_t latest_event;

    /* Link tracking for recovery */
    bool last_link_fresh;
    bool last_timeout_latched;
    uint32_t last_frame_sequence;
    bool initialized;
} BladeConfirmCh6Adapter_t;

typedef struct
{
    uint16_t raw;
    BladeConfirmCh6Position_t candidate_position;
    uint8_t candidate_count;
    uint8_t confirm_frames;
    uint32_t event_sequence;
    bool event_available;
    uint32_t event_request_sequence;
} BladeConfirmCh6Diagnostics_t;

BladeConfirmCh6Config_t BladeConfirmCh6Adapter_DefaultConfig(void);
void BladeConfirmCh6Adapter_Init(BladeConfirmCh6Adapter_t *adapter,
                                 const BladeConfirmCh6Config_t *config);
void BladeConfirmCh6Adapter_Reset(BladeConfirmCh6Adapter_t *adapter);
void BladeConfirmCh6Adapter_Advance(BladeConfirmCh6Adapter_t *adapter,
                                    const BladeConfirmCh6Input_t *input);

BladeConfirmCh6Position_t BladeConfirmCh6Adapter_ClassifyCh6(
    const BladeConfirmCh6Config_t *config, uint16_t pulse_us);

bool BladeConfirmCh6Adapter_IsBaselineObserved(const BladeConfirmCh6Adapter_t *adapter);
BladeConfirmCh6Position_t BladeConfirmCh6Adapter_GetCurrentPosition(const BladeConfirmCh6Adapter_t *adapter);
uint32_t BladeConfirmCh6Adapter_GetEventSequence(const BladeConfirmCh6Adapter_t *adapter);
bool BladeConfirmCh6Adapter_HasEvent(const BladeConfirmCh6Adapter_t *adapter);
void BladeConfirmCh6Adapter_GetDiagnostics(const BladeConfirmCh6Adapter_t *adapter,
                                           BladeConfirmCh6Diagnostics_t *diagnostics);

bool BladeConfirmCh6Adapter_GetConfirmOK(BladeConfirmCh6Adapter_t *adapter,
                                        uint32_t expected_request_sequence,
                                        BladeConfirmOK_t *ok);

void BladeConfirmCh6Adapter_ConsumeEvent(BladeConfirmCh6Adapter_t *adapter,
                                        uint32_t event_sequence);

#endif /* BLADE_CONFIRM_CH6_ADAPTER_H */
