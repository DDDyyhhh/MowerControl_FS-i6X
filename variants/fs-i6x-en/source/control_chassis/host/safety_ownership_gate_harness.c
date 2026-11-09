#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "safety_ownership_gate.h"

static SafetyOwnershipGateInput_t AllowedManual(uint32_t sequence)
{
    SafetyOwnershipGateInput_t input = {
        false,
        false,
        false,
        true,
        true,
        EXECUTION_OWNER_MANUAL_REMOTE,
        sequence,
        0U};
    return input;
}

static void TestEveryRejectConditionRequestsStop(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(1U);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(!SafetyOwnershipGate_TakeStopRequest());

    input.physical_latched = true;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input = AllowedManual(2U);
    input.software_locked = true;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input = AllowedManual(3U);
    input.recoverable_remote_latched = true;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input = AllowedManual(4U);
    input.link_stable = false;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input = AllowedManual(5U);
    input.run_permitted = false;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input = AllowedManual(6U);
    input.selected_owner = EXECUTION_OWNER_NONE;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());
}

static void TestOwnerMismatchRejectsApiAndCruise(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(10U);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API, true));
    assert(SafetyOwnershipGate_TakeStopRequest());
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_CRUISE, true));
}

static void TestRecoveryRequiresANewCommand(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(20U);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.command_invalidation_sequence = 1U;
    input.link_stable = false;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
    assert(SafetyOwnershipGate_TakeStopRequest());

    input.link_stable = true;
    input.run_permitted = true;
    input.recoverable_remote_latched = false;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.command_sequence = 21U;
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
}

static void TestOwnershipChangeRequiresANewCommand(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(30U);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.selected_owner = EXECUTION_OWNER_NONE;
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_TakeStopRequest());

    input.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.command_sequence = 31U;
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
}

static void TestZeroIntentIsAlwaysAllowed(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(40U);

    input.physical_latched = true;
    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_API, false));
}

static void TestSequenceWrapStillAcceptsFreshCommand(void)
{
    SafetyOwnershipGateInput_t input = AllowedManual(UINT32_MAX);

    SafetyOwnershipGate_Reset();
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.link_stable = false;
    input.command_invalidation_sequence = 1U;
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_TakeStopRequest());

    input.link_stable = true;
    SafetyOwnershipGate_Update(&input);
    assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));

    input.command_sequence = 0U;
    SafetyOwnershipGate_Update(&input);
    assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE, true));
}

int main(void)
{
    TestEveryRejectConditionRequestsStop();
    TestOwnerMismatchRejectsApiAndCruise();
    TestRecoveryRequiresANewCommand();
    TestOwnershipChangeRequiresANewCommand();
    TestZeroIntentIsAlwaysAllowed();
    TestSequenceWrapStillAcceptsFreshCommand();
    return 0;
}
