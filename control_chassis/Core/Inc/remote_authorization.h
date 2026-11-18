#ifndef REMOTE_AUTHORIZATION_H
#define REMOTE_AUTHORIZATION_H

#include <stdbool.h>
#include <stdint.h>

#include "remote_backend_config.h"

typedef enum
{
    REMOTE_AUTH_SYNCHRONIZING = 0,
    REMOTE_AUTH_WAIT_STABLE_OFF,
    REMOTE_AUTH_STOPPED_LATCHED,
    REMOTE_AUTH_WAIT_UNLOCK_EDGE_1,
    REMOTE_AUTH_WAIT_UNLOCK_EDGE_2,
    REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY,
    REMOTE_AUTH_MANUAL_ARMED,
    REMOTE_AUTH_WAIT_OK
} RemoteAuthorizationState_t;

typedef enum
{
    REMOTE_SWITCH_UNKNOWN = 0,
    REMOTE_SWITCH_OFF,
    REMOTE_SWITCH_ON
} RemoteRunEnableState_t;

typedef enum
{
    REMOTE_UNLOCK_UNKNOWN = 0,
    REMOTE_UNLOCK_UP,
    REMOTE_UNLOCK_MIDDLE,
    REMOTE_UNLOCK_DOWN
} RemoteUnlockState_t;
typedef enum
{
    REMOTE_BLADE_GEAR_UNKNOWN = 0,
    REMOTE_BLADE_GEAR_LOW = 1,
    REMOTE_BLADE_GEAR_MEDIUM = 2,
    REMOTE_BLADE_GEAR_HIGH = 3
} RemoteBladeGear_t;

typedef enum
{
    REMOTE_LATCH_REASON_NONE = 0,
    REMOTE_LATCH_REASON_SYNCHRONIZING,
    REMOTE_LATCH_REASON_TIMEOUT,
    REMOTE_LATCH_REASON_REMOTE_STOP,
    REMOTE_LATCH_REASON_OWNERSHIP_TRANSITION,
    REMOTE_LATCH_REASON_SAFETY_REJECT
} RemoteRecoverableLatchReason_t;


typedef struct
{
    bool link_fresh;
    /* Current timeout, distinct from a historical timeout latch. */
    bool link_timed_out;
    bool stable_online;
    bool physical_latched;
    bool emergency_active;
    bool collision_active;
    bool tilt_active;
    uint16_t steering_raw;
    uint16_t throttle_raw;
    uint16_t run_enable_raw;
    uint16_t blade_gear_raw;
    uint16_t unlock_raw;
    uint16_t ch4_raw;
    uint32_t frame_sequence;
    uint32_t timeout_sequence;
    uint32_t ch5_stop_sequence;
} RemoteAuthorizationInput_t;

typedef struct
{
    RemoteAuthorizationState_t state;
    RemoteRunEnableState_t run_enable;
    RemoteUnlockState_t unlock;
    bool remote_recoverable_latched;
    RemoteBladeGear_t blade_gear;
    bool run_permitted;
    uint32_t command_invalidation_sequence;
    RemoteRecoverableLatchReason_t latch_reason;
} RemoteAuthorizationOutput_t;

typedef struct
{
    FlySkyIbusConfig_t config;
    RemoteAuthorizationOutput_t output;
    RemoteRunEnableState_t run_candidate;
    RemoteUnlockState_t unlock_candidate;
    RemoteBladeGear_t blade_gear_candidate;
    uint8_t run_candidate_count;
    uint8_t unlock_candidate_count;
    bool run_baseline_valid;
    uint8_t blade_gear_candidate_count;
    bool unlock_baseline_valid;
    bool centered;
    bool ch4_center_seen;
    bool ch4_full;
    uint32_t center_started_ms;
    uint32_t unlock_started_ms;
    uint32_t last_frame_sequence;
    uint32_t armed_frame_sequence;
    uint32_t last_timeout_sequence;
    uint32_t last_ch5_stop_sequence;
} RemoteAuthorization_t;

void RemoteAuthorization_Init(RemoteAuthorization_t *authorization,
                              const FlySkyIbusConfig_t *config);
void RemoteAuthorization_Advance(RemoteAuthorization_t *authorization,
                                 const RemoteAuthorizationInput_t *input,
                                 uint32_t now_ms);
void RemoteAuthorization_GetOutput(const RemoteAuthorization_t *authorization,
                                   RemoteAuthorizationOutput_t *output);
/* Motion task only. An OK is consumed immediately, never queued for safety recovery. */
bool RemoteAuthorization_ConfirmOK(RemoteAuthorization_t *authorization,
                                  const RemoteAuthorizationInput_t *input,
                                  uint32_t now_ms);
/* Environmental checks shared by the center phase and actual confirmation. */
uint8_t RemoteAuthorization_BlockReason(const RemoteAuthorization_t *authorization,
                                       const RemoteAuthorizationInput_t *input, uint32_t now_ms);
void RemoteAuthorization_CancelOK(RemoteAuthorization_t *authorization);
void RemoteAuthorization_Revoke(RemoteAuthorization_t *authorization);

#endif
