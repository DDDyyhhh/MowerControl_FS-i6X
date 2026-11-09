#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "remote_authorization.h"

static RemoteAuthorization_t g_authorization;
static RemoteAuthorizationInput_t g_input;
static uint32_t g_now_ms;

static FlySkyIbusConfig_t TestConfig(void)
{
    FlySkyIbusConfig_t config = {0};

    config.steering.center_us = 1500U;
    config.throttle.center_us = 1500U;
    config.run_enable_off_us = 1000U;
    config.run_enable_on_us = 2000U;
    config.unlock_up_us = 1000U;
    config.unlock_middle_us = 1500U;
    config.unlock_down_us = 2000U;
    config.blade_gear_low_us = 2000U;
    config.blade_gear_medium_us = 1500U;
    config.blade_gear_high_us = 1000U;
    config.switch_position_tolerance_us = 100U;
    config.stick_center_tolerance_us = 50U;
    config.stick_center_hold_ms = 200U;
    config.unlock_window_ms = 3000U;
    config.switch_confirm_frames = 3U;
    config.run_enable_semantics_configured = true;
    return config;
}

static RemoteAuthorizationOutput_t ReadOutput(void)
{
    RemoteAuthorizationOutput_t output;
    RemoteAuthorization_GetOutput(&g_authorization, &output);
    return output;
}

static void InitAuthorization(void)
{
    FlySkyIbusConfig_t config = TestConfig();

    RemoteAuthorization_Init(&g_authorization, &config);
    g_input = (RemoteAuthorizationInput_t){0};
    g_input.link_fresh = true;
    g_input.stable_online = true;
    g_input.steering_raw = 1500U;
    g_input.throttle_raw = 1500U;
    g_input.run_enable_raw = 1000U;
    g_input.blade_gear_raw = 2000U;
    g_input.unlock_raw = 2000U;
    g_now_ms = 0U;
}

static void SendFrame(uint32_t now_ms, uint16_t run_enable, uint16_t unlock,
                      uint16_t steering, uint16_t throttle)
{
    g_now_ms = now_ms;
    g_input.frame_sequence++;
    g_input.run_enable_raw = run_enable;
    g_input.unlock_raw = unlock;
    g_input.steering_raw = steering;
    g_input.throttle_raw = throttle;
    RemoteAuthorization_Advance(&g_authorization, &g_input, g_now_ms);
}

static void ConfirmSwitches(uint32_t start_ms, uint16_t run_enable,
                            uint16_t unlock)
{
    SendFrame(start_ms, run_enable, unlock, 1500U, 1500U);
    SendFrame(start_ms + 10U, run_enable, unlock, 1500U, 1500U);
    SendFrame(start_ms + 20U, run_enable, unlock, 1500U, 1500U);
}

static void ReachUnlockValidation(void)
{
    ConfirmSwitches(0U, 2000U, 2000U);
    ConfirmSwitches(30U, 2000U, 1000U);
    ConfirmSwitches(60U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY);
}

static void TestStartupOnAllowsDirectCh8Unlock(void)
{
    RemoteAuthorizationOutput_t output;

    InitAuthorization();
    ConfirmSwitches(0U, 2000U, 2000U);
    output = ReadOutput();
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(output.remote_recoverable_latched);
    assert(!output.run_permitted);

    ConfirmSwitches(30U, 2000U, 1000U);
    assert(ReadOutput().state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2);
    ConfirmSwitches(60U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY);
}

static void TestUnlockRequiresTwoAdjacentChangesAndCenterHold(void)
{
    RemoteAuthorizationOutput_t output;

    InitAuthorization();
    ReachUnlockValidation();
    RemoteAuthorization_Advance(&g_authorization, &g_input, 309U);
    assert(!ReadOutput().run_permitted);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 310U);
    output = ReadOutput();
    assert(output.state == REMOTE_AUTH_MANUAL_ARMED);
    assert(!output.run_permitted);
    assert(!output.remote_recoverable_latched);
    assert(output.command_invalidation_sequence >= 1U);
    SendFrame(320U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);
}

static void TestDirectUnlockJumpAndOneEdgeCannotArm(void)
{
    InitAuthorization();
    ConfirmSwitches(0U, 2000U, 2000U);
    ConfirmSwitches(30U, 2000U, 1500U);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);

    ConfirmSwitches(60U, 2000U, 1000U);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 500U);
    assert(!ReadOutput().run_permitted);
    assert(ReadOutput().state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2);
}

static void TestCenterAndPhysicalSafetyResetAttempt(void)
{
    InitAuthorization();
    ReachUnlockValidation();
    SendFrame(120U, 2000U, 2000U, 1600U, 1500U);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 400U);
    assert(!ReadOutput().run_permitted);

    g_input.collision_active = true;
    RemoteAuthorization_Advance(&g_authorization, &g_input, 410U);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(ReadOutput().remote_recoverable_latched);
}

static void TestTimeoutAndRecoveryDoNotReplayAuthorization(void)
{
    RemoteAuthorizationOutput_t output;

    InitAuthorization();
    ReachUnlockValidation();
    RemoteAuthorization_Advance(&g_authorization, &g_input, 310U);
    assert(!ReadOutput().run_permitted);
    SendFrame(320U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);

    g_input.link_fresh = false;
    g_input.stable_online = false;
    g_input.timeout_sequence++;
    RemoteAuthorization_Advance(&g_authorization, &g_input, 330U);
    output = ReadOutput();
    assert(output.state == REMOTE_AUTH_SYNCHRONIZING);
    assert(!output.run_permitted);
    assert(output.remote_recoverable_latched);

    g_input.link_fresh = true;
    g_input.stable_online = true;
    ConfirmSwitches(340U, 2000U, 2000U);
    output = ReadOutput();
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(!output.run_permitted);
}

static void TestRunEnableEdgeStopsArmedState(void)
{
    uint32_t invalidation_before;

    InitAuthorization();
    ReachUnlockValidation();
    RemoteAuthorization_Advance(&g_authorization, &g_input, 310U);
    SendFrame(320U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);
    invalidation_before = ReadOutput().command_invalidation_sequence;
    ConfirmSwitches(330U, 1000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(!ReadOutput().run_permitted);
    assert(ReadOutput().remote_recoverable_latched);
    assert(ReadOutput().command_invalidation_sequence > invalidation_before);
}
static void TestSpeedGearRequiresThreeConfirmedFrames(void)
{
    InitAuthorization();
    g_input.blade_gear_raw = 2012U;
    SendFrame(0U, 1000U, 2000U, 1500U, 1500U);
    SendFrame(10U, 1000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().blade_gear == REMOTE_BLADE_GEAR_UNKNOWN);
    SendFrame(20U, 1000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().blade_gear == REMOTE_BLADE_GEAR_LOW);

    g_input.blade_gear_raw = 1500U;
    SendFrame(30U, 1000U, 2000U, 1500U, 1500U);
    SendFrame(40U, 1000U, 2000U, 1500U, 1500U);
    SendFrame(50U, 1000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().blade_gear == REMOTE_BLADE_GEAR_MEDIUM);

    g_input.blade_gear_raw = 1000U;
    SendFrame(60U, 1000U, 2000U, 1500U, 1500U);
    SendFrame(70U, 1000U, 2000U, 1500U, 1500U);
    SendFrame(80U, 1000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().blade_gear == REMOTE_BLADE_GEAR_HIGH);
}
static void TestPhysicalSafetyCanBeUnlockedAfterActiveClear(void)
{
    InitAuthorization();
    ReachUnlockValidation();
    RemoteAuthorization_Advance(&g_authorization, &g_input, 310U);
    SendFrame(320U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);

    /* 1. Emergency active -> immediate latch & disarm */
    g_input.emergency_active = true;
    RemoteAuthorization_Advance(&g_authorization, &g_input, 330U);
    assert(!ReadOutput().run_permitted);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);

    /* 2. Trying to unlock while emergency is still active -> blocked */
    ConfirmSwitches(340U, 2000U, 1000U);
    ConfirmSwitches(370U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(!ReadOutput().run_permitted);

    /* 3. Emergency button released -> CH8 unlock succeeds */
    g_input.emergency_active = false;
    ConfirmSwitches(400U, 2000U, 1000U);
    assert(ReadOutput().state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2);
    ConfirmSwitches(430U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 660U);
    assert(ReadOutput().state == REMOTE_AUTH_MANUAL_ARMED);
    assert(!ReadOutput().run_permitted);
    SendFrame(670U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);

    /* 4. Collision active -> immediate latch & disarm */
    g_input.collision_active = true;
    RemoteAuthorization_Advance(&g_authorization, &g_input, 680U);
    assert(!ReadOutput().run_permitted);
    assert(ReadOutput().state == REMOTE_AUTH_STOPPED_LATCHED);

    /* 5. Collision bumper freed -> CH8 unlock succeeds */
    g_input.collision_active = false;
    ConfirmSwitches(690U, 2000U, 1000U);
    assert(ReadOutput().state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2);
    ConfirmSwitches(720U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 950U);
    assert(ReadOutput().state == REMOTE_AUTH_MANUAL_ARMED);
    assert(!ReadOutput().run_permitted);
    SendFrame(960U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);
}

static void TestCh7SpeedGearDoesNotAdvanceUnlockAndCh8IsExclusiveUnlock(void)
{
    RemoteAuthorizationOutput_t output;

    InitAuthorization();
    /* Establish baseline with run_enable ON, unlock at baseline 2000 */
    ConfirmSwitches(0U, 2000U, 2000U);
    output = ReadOutput();
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED);
    assert(!output.run_permitted);

    /* 1. Cycle CH7 (Speed Gear) through all positions while CH8 remains at baseline */
    /* Set CH7 to MEDIUM (1500 us) */
    g_input.blade_gear_raw = 1500U;
    SendFrame(30U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(40U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(50U, 2000U, 2000U, 1500U, 1500U);
    output = ReadOutput();
    assert(output.blade_gear == REMOTE_BLADE_GEAR_MEDIUM);
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED); /* UNLOCK NOT ADVANCED */
    assert(!output.run_permitted);

    /* Set CH7 to HIGH (1000 us) */
    g_input.blade_gear_raw = 1000U;
    SendFrame(60U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(70U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(80U, 2000U, 2000U, 1500U, 1500U);
    output = ReadOutput();
    assert(output.blade_gear == REMOTE_BLADE_GEAR_HIGH);
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED); /* UNLOCK NOT ADVANCED */
    assert(!output.run_permitted);

    /* Set CH7 back to LOW (2000 us) */
    g_input.blade_gear_raw = 2000U;
    SendFrame(90U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(100U, 2000U, 2000U, 1500U, 1500U);
    SendFrame(110U, 2000U, 2000U, 1500U, 1500U);
    output = ReadOutput();
    assert(output.blade_gear == REMOTE_BLADE_GEAR_LOW);
    assert(output.state == REMOTE_AUTH_STOPPED_LATCHED); /* UNLOCK NOT ADVANCED */
    assert(!output.run_permitted);

    /* 2. CH8 operates the real unlock sequence */
    ConfirmSwitches(120U, 2000U, 1000U);
    assert(ReadOutput().state == REMOTE_AUTH_WAIT_UNLOCK_EDGE_2);
    ConfirmSwitches(150U, 2000U, 2000U);
    assert(ReadOutput().state == REMOTE_AUTH_VALIDATE_CENTER_AND_SAFETY);
    RemoteAuthorization_Advance(&g_authorization, &g_input, 400U);
    assert(ReadOutput().state == REMOTE_AUTH_MANUAL_ARMED);
    assert(!ReadOutput().run_permitted);
    SendFrame(410U, 2000U, 2000U, 1500U, 1500U);
    assert(ReadOutput().run_permitted);
}


int main(void)
{
    TestStartupOnAllowsDirectCh8Unlock();
    TestUnlockRequiresTwoAdjacentChangesAndCenterHold();
    TestDirectUnlockJumpAndOneEdgeCannotArm();
    TestCenterAndPhysicalSafetyResetAttempt();
    TestSpeedGearRequiresThreeConfirmedFrames();
    TestTimeoutAndRecoveryDoNotReplayAuthorization();
    TestRunEnableEdgeStopsArmedState();
    TestPhysicalSafetyCanBeUnlockedAfterActiveClear();
    TestCh7SpeedGearDoesNotAdvanceUnlockAndCh8IsExclusiveUnlock();
    return 0;
}
