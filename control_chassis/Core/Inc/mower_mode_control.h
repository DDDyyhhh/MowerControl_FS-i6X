#ifndef MOWER_MODE_CONTROL_H
#define MOWER_MODE_CONTROL_H
#include "remote_backend_config.h"
#include "remote_authorization.h"
#include "../../../experiments/ticket03/mower_control_contract.h"
typedef enum { MOWER_SELECT_RC = 0, MOWER_SELECT_API = 1 } MowerSelectedMode_t;
typedef struct {
    bool online, enabled, safety_active, centered, blade_centered;
    uint16_t mode_raw;
    uint32_t frame_sequence;
} MowerModeInput_t;
typedef struct {
    MowerSelectedMode_t selected;
    RemoteRecoverableLatchReason_t latch_reason;
    uint8_t api_mode, candidate, candidate_count;
    bool mode_valid, armed, ready, permitted, center_tracking, stop_requested;
    bool session_bound, generation_exhausted, online, enabled;
    uint16_t generation;
    uint32_t mode_sequence, invalidation_sequence, frame_sequence, armed_frame;
    uint32_t center_started, host_tick, controller, hmi;
} MowerModeControl_t;
void MowerMode_Init(MowerModeControl_t *state);
void MowerMode_Advance(MowerModeControl_t *state, const MowerModeInput_t *input, uint32_t now);
void MowerMode_Session(MowerModeControl_t *state, uint32_t controller, uint32_t hmi);
void MowerMode_Stop(MowerModeControl_t *state);
bool MowerMode_Confirm(MowerModeControl_t *state, uint32_t generation);
bool MowerMode_Cancel(MowerModeControl_t *state, uint32_t generation);
bool MowerMode_Host(MowerModeControl_t *state, uint8_t command, uint32_t tick);
uint16_t MowerMode_Status(const MowerModeControl_t *state, uint32_t now);
#endif
