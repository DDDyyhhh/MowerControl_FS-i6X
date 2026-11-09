#include "safety_ownership_gate.h"

#include <string.h>

typedef struct
{
    SafetyOwnershipGateInput_t input;
    uint32_t invalidated_sequence;
    uint32_t last_command_invalidation_sequence;
    ExecutionOwner_t last_owner;
    bool initialized;
    bool input_allowed;
    bool fresh_command_required;
    bool stop_requested;
} SafetyOwnershipGateState_t;

static SafetyOwnershipGateState_t g_gate;

static bool SafetyOwnershipGate_InputAllowsNonzero(void)
{
    return !g_gate.input.physical_latched &&
           !g_gate.input.software_locked &&
           !g_gate.input.recoverable_remote_latched &&
           g_gate.input.link_stable &&
           g_gate.input.run_permitted &&
           (g_gate.input.selected_owner != EXECUTION_OWNER_NONE);
}

static void SafetyOwnershipGate_InvalidateCurrentCommand(void)
{
    g_gate.invalidated_sequence = g_gate.input.command_sequence;
    g_gate.fresh_command_required = true;
    g_gate.stop_requested = true;
}

void SafetyOwnershipGate_Reset(void)
{
    memset(&g_gate, 0, sizeof(g_gate));
    g_gate.last_owner = EXECUTION_OWNER_NONE;
}

void SafetyOwnershipGate_Update(const SafetyOwnershipGateInput_t *input)
{
    bool owner_changed;
    bool command_invalidated;
    bool authorization_denied;

    if (input == NULL)
    {
        return;
    }

    owner_changed = g_gate.initialized &&
                    (input->selected_owner != g_gate.last_owner);
    command_invalidated = g_gate.initialized &&
                          (input->command_invalidation_sequence !=
                           g_gate.last_command_invalidation_sequence);

    g_gate.input = *input;
    authorization_denied = !SafetyOwnershipGate_InputAllowsNonzero();

    if (owner_changed || command_invalidated ||
        (g_gate.initialized && g_gate.input_allowed && authorization_denied))
    {
        SafetyOwnershipGate_InvalidateCurrentCommand();
    }
    g_gate.input_allowed = !authorization_denied;

    g_gate.last_owner = input->selected_owner;
    g_gate.last_command_invalidation_sequence =
        input->command_invalidation_sequence;
    g_gate.initialized = true;
}

bool SafetyOwnershipGate_Authorize(ExecutionOwner_t intent_owner,
                                   bool nonzero_intent)
{
    if (!nonzero_intent)
    {
        return true;
    }

    if (!SafetyOwnershipGate_InputAllowsNonzero() ||
        (intent_owner != g_gate.input.selected_owner) ||
        (g_gate.fresh_command_required &&
         (g_gate.input.command_sequence == g_gate.invalidated_sequence)))
    {
        g_gate.stop_requested = true;
        return false;
    }

    g_gate.fresh_command_required = false;

    return true;
}

bool SafetyOwnershipGate_IsOwnerPermitted(ExecutionOwner_t owner)
{
    return g_gate.initialized &&
           SafetyOwnershipGate_InputAllowsNonzero() &&
           (owner == g_gate.input.selected_owner) &&
           !(g_gate.fresh_command_required &&
             (g_gate.input.command_sequence == g_gate.invalidated_sequence));
}

SafetyOwnershipGateReason_t SafetyOwnershipGate_GetReason(ExecutionOwner_t owner)
{
    if (!g_gate.initialized)
    {
        return SAFETY_GATE_REJECT_NOT_INITIALIZED;
    }
    if (g_gate.input.physical_latched)
    {
        return SAFETY_GATE_REJECT_PHYSICAL;
    }
    if (g_gate.input.software_locked)
    {
        return SAFETY_GATE_REJECT_SOFTWARE_LOCK;
    }
    if (g_gate.input.recoverable_remote_latched)
    {
        return SAFETY_GATE_REJECT_REMOTE_LATCH;
    }
    if (!g_gate.input.link_stable)
    {
        return SAFETY_GATE_REJECT_LINK;
    }
    if (!g_gate.input.run_permitted)
    {
        return SAFETY_GATE_REJECT_RUN_PERMISSION;
    }
    if ((g_gate.input.selected_owner == EXECUTION_OWNER_NONE) ||
        (owner != g_gate.input.selected_owner))
    {
        return SAFETY_GATE_REJECT_OWNER;
    }
    if (g_gate.fresh_command_required &&
        (g_gate.input.command_sequence == g_gate.invalidated_sequence))
    {
        return SAFETY_GATE_REJECT_STALE_COMMAND;
    }
    return SAFETY_GATE_PERMIT;
}

bool SafetyOwnershipGate_TakeStopRequest(void)
{
    bool requested = g_gate.stop_requested;
    g_gate.stop_requested = false;
    return requested;
}
