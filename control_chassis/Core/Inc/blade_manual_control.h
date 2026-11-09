#ifndef BLADE_MANUAL_CONTROL_H
#define BLADE_MANUAL_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "blade_hmi_contract.h"
#include "blade_motor_config.h"
#include "remote_backend_config.h"
#include "remote_authorization.h"
#include "safety_ownership_gate.h"

typedef enum
{
    BLADE_CH4_POS_UNKNOWN = 0,
    BLADE_CH4_POS_CENTER,
    BLADE_CH4_POS_START,
    BLADE_CH4_POS_OTHER
} BladeCh4Position_t;
typedef struct
{
    /* Centralized CH4 classification parameters (for classifier helper) */
    uint16_t ch4_center_us;
    uint16_t ch4_start_us;
    uint16_t ch4_tolerance_us;

    /* Three-position CH7 blade gear switch. */
    uint16_t ch7_high_us;
    uint16_t ch7_medium_us;
    uint16_t ch7_low_us;
    uint16_t ch7_tolerance_us;

    /* Driver-specific output limits */
    float output_min;
    float output_medium;
    float output_max;
    float output_deadzone;
} BladeManualControlConfig_t;

typedef struct
{
    /* Link freshness and frame sequence */
    bool link_fresh;
    bool timeout_latched;
    uint32_t frame_sequence;

    /* Safety interlocks and ownership */
    bool physical_latched;
    bool emergency_active;
    bool collision_active;
    bool tilt_active;
    bool software_locked;
    bool remote_recoverable_latched;
    bool remote_run_enable;
    bool run_permitted;
    bool run_enable_changed;
    ExecutionOwner_t selected_owner;
    bool owner_changed;
    uint32_t command_invalidation_sequence;
    bool command_invalidated;
    bool gate_stop_requested;

    /* Operator inputs */
    BladeCh4Position_t ch4_position;
    uint16_t ch4_raw;
    uint16_t ch7_raw;
    RemoteBladeGear_t confirmed_gear;
    float blade_speed_override;

    /* Logical confirmation event */
    bool has_confirm_event;
    uint32_t confirm_event_sequence;
    BladeConfirmOK_t confirm_event;
} BladeManualControlInput_t;

typedef struct
{
    BladeManualState_t state;
    BladeManualControlConfig_t config;

    /* Request sequence and event tracking */
    uint32_t request_sequence;
    uint32_t baseline_event_sequence;
    uint32_t last_consumed_event_sequence;
    bool request_active;

    /* Injected confirm event buffer */
    bool has_injected_event;
    BladeConfirmOK_t injected_event;

    /* Baseline and edge tracking */
    bool center_baseline_observed;
    bool start_armed;
    BladeCh4Position_t last_ch4_position;

    /* Command tracking & anti-replay */
    uint32_t confirmed_frame_sequence;
    bool fresh_gear_received;
    uint32_t last_invalidation_sequence;
    ExecutionOwner_t last_owner;
    bool last_run_enable;
    bool initialized;

    /* Current outputs */
    float commanded_speed;
    BladeStartConfirmRequest_t request_output;
} BladeManualControl_t;

BladeManualControlConfig_t BladeManualControl_DefaultConfig(void);
void BladeManualControl_Init(BladeManualControl_t *ctrl,
                             const BladeManualControlConfig_t *config);
void BladeManualControl_Reset(BladeManualControl_t *ctrl);
void BladeManualControl_Step(BladeManualControl_t *ctrl,
                             const BladeManualControlInput_t *input);

BladeManualState_t BladeManualControl_GetState(const BladeManualControl_t *ctrl);
float BladeManualControl_GetCommand(const BladeManualControl_t *ctrl);
bool BladeManualControl_GetStartConfirmRequest(const BladeManualControl_t *ctrl,
                                              BladeStartConfirmRequest_t *request);
uint32_t BladeManualControl_GetRequestSequence(const BladeManualControl_t *ctrl);
bool BladeManualControl_IsCenterBaselineObserved(const BladeManualControl_t *ctrl);
void BladeManualControl_InjectConfirmOK(BladeManualControl_t *ctrl,
                                       const BladeConfirmOK_t *confirm_event);

BladeCh4Position_t BladeManualControl_ClassifyCh4(const BladeManualControlConfig_t *config,
                                                  uint16_t pulse_us);
float BladeManualControl_MapSpeed(const BladeManualControlConfig_t *config,
                                  uint16_t ch7_us);

#endif
