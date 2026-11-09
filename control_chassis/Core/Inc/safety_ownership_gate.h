#ifndef SAFETY_OWNERSHIP_GATE_H
#define SAFETY_OWNERSHIP_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EXECUTION_OWNER_NONE = 0,
    EXECUTION_OWNER_MANUAL_REMOTE,
    EXECUTION_OWNER_API,
    EXECUTION_OWNER_CRUISE
} ExecutionOwner_t;

typedef enum
{
    SAFETY_GATE_PERMIT = 0,
    SAFETY_GATE_REJECT_NOT_INITIALIZED,
    SAFETY_GATE_REJECT_PHYSICAL,
    SAFETY_GATE_REJECT_SOFTWARE_LOCK,
    SAFETY_GATE_REJECT_REMOTE_LATCH,
    SAFETY_GATE_REJECT_LINK,
    SAFETY_GATE_REJECT_RUN_PERMISSION,
    SAFETY_GATE_REJECT_OWNER,
    SAFETY_GATE_REJECT_STALE_COMMAND
} SafetyOwnershipGateReason_t;

typedef struct
{
    bool physical_latched;
    bool software_locked;
    bool recoverable_remote_latched;
    bool link_stable;
    bool run_permitted;
    ExecutionOwner_t selected_owner;
    uint32_t command_sequence;
    uint32_t command_invalidation_sequence;
} SafetyOwnershipGateInput_t;

void SafetyOwnershipGate_Reset(void);
void SafetyOwnershipGate_Update(const SafetyOwnershipGateInput_t *input);
bool SafetyOwnershipGate_Authorize(ExecutionOwner_t intent_owner,
                                   bool nonzero_intent);
bool SafetyOwnershipGate_IsOwnerPermitted(ExecutionOwner_t owner);
bool SafetyOwnershipGate_TakeStopRequest(void);
SafetyOwnershipGateReason_t SafetyOwnershipGate_GetReason(ExecutionOwner_t owner);

#endif
