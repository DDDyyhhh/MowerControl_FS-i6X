#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "remote_backend.h"
#include "flysky_ibus_servo.h"

static uint32_t g_now_ms;

static uint32_t HarnessNow(void *context)
{
    (void)context;
    return g_now_ms;
}

#if REMOTE_BACKEND_FLYSKY_IBUS
static FlySkyIbusConfig_t TestConfig(void)
{
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    config.run_enable_semantics_configured = true;
    config.blade_height_semantics_configured = true;
    config.travel_speed_semantics_configured = true;
    config.blade_start_semantics_configured = true;
    config.blade_confirm_semantics_configured = true;
    config.grass_bag_semantics_configured = false;
    return config;
}

static void BuildIbusFrame(uint8_t frame[REMOTE_IBUS_FRAME_SIZE],
                           const uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT])
{
    uint16_t sum = 0U;
    uint16_t checksum;
    uint8_t i;

    frame[0] = 0x20U;
    frame[1] = 0x40U;
    for (i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; i++)
    {
        frame[2U + (2U * i)] = (uint8_t)channels[i];
        frame[3U + (2U * i)] = (uint8_t)(channels[i] >> 8);
    }
    for (i = 0U; i < 30U; i++)
    {
        sum = (uint16_t)(sum + frame[i]);
    }
    checksum = (uint16_t)(0xFFFFU - sum);
    frame[30] = (uint8_t)checksum;
    frame[31] = (uint8_t)(checksum >> 8);
}

static void SetNormalIbusChannels(uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT])
{
    uint8_t i;

    for (i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; i++)
    {
        channels[i] = 1500U;
    }
    channels[0] = 1200U;
    channels[1] = 1800U;
    channels[3] = 1500U;
    channels[4] = 2000U;
    channels[5] = 1000U;
    channels[6] = 2012U;
    channels[7] = 2000U;
    channels[8] = 1700U;
    channels[9] = 1600U;
}
static void InitFlySky(const FlySkyIbusConfig_t *config)
{
    RemoteBackendClock_t clock = {HarnessNow, NULL};

    g_now_ms = 0U;
    RemoteBackend_InitWithFlySkyConfig(clock, config);
}

static void SendIbusFrameAt(uint32_t now_ms,
                            const uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT])
{
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];

    BuildIbusFrame(frame, channels);
    g_now_ms = now_ms;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
}

static RemoteCommand_t ReadCommand(void)
{
    RemoteCommand_t command;

    assert(RemoteBackend_GetCommand(&command));
    return command;
}

static void TestBladeGearUsesCh7NotCh10(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;
    uint32_t time_ms = 10U;
    const uint16_t gear_us[] = {2000U, 1500U, 1000U};
    const RemoteBladeGear_t gears[] = {
        REMOTE_BLADE_GEAR_LOW, REMOTE_BLADE_GEAR_MEDIUM,
        REMOTE_BLADE_GEAR_HIGH
    };

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    channels[0] = 1500U;
    channels[1] = 1500U;
    for (uint8_t frame = 0U; frame < 25U; frame++)
    {
        SendIbusFrameAt(time_ms, channels);
        RemoteBackend_Advance(time_ms,
                              (RemoteSafetyInput_t){false, false, false, false});
        time_ms += 10U;
    }
    for (size_t gear = 0U; gear < 3U; gear++)
    {
        channels[6] = gear_us[gear];
        for (uint8_t frame = 0U; frame < 3U; frame++)
        {
            channels[9] = frame == 0U ? 1000U :
                          frame == 1U ? 1500U : 2000U;
            SendIbusFrameAt(time_ms, channels);
            RemoteBackend_Advance(time_ms,
                                  (RemoteSafetyInput_t){false, false, false, false});
            time_ms += 10U;
        }
        command = ReadCommand();
        assert(command.blade_gear_raw == gear_us[gear]);
        assert(command.blade_gear == gears[gear]);
        assert(command.channels[9] == 2000U);
    }
}

static void TestValidSplitFramePublishesSnapshot(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    BuildIbusFrame(frame, channels);
    g_now_ms = 10U;
    RemoteBackend_OnUartRx(1U, frame, 7U);
    assert(!RemoteBackend_GetCommand(&command));
    RemoteBackend_OnUartRx(1U, &frame[7], (uint16_t)(sizeof(frame) - 7U));
    command = ReadCommand();

    assert(command.frame_sequence == 1U);
    assert(command.steering == 2048U);
    assert(command.throttle == 2048U);
    assert(command.run_enable_raw == 2000U);
    assert(command.blade_gear_raw == 2012U);
    assert(command.unlock_raw == 2000U);
    assert(command.gear == REMOTE_BLADE_GEAR_UNKNOWN);
    assert(command.blade_height == 1700U);
    assert(command.travel_speed_raw == 0U);
    assert(command.travel_speed_mps == 0.0f);
    assert(command.link_fresh);
    assert(!command.stable_online);
    assert(!command.connected);
    assert(!command.run_permitted);
    channels[0] = 1500U;
    channels[1] = 1500U;
    SendIbusFrameAt(11U, channels);
    command = ReadCommand();
    assert(command.steering == 2048U);
    assert(command.throttle == 2048U);
}

static void TestNoiseDroppedByteAndBadFrameResynchronize(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    uint8_t stream[(REMOTE_IBUS_FRAME_SIZE * 3U) + 5U];
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;
    uint16_t offset = 0U;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    BuildIbusFrame(frame, channels);

    stream[offset++] = 0x91U;
    stream[offset++] = 0x20U;
    stream[offset++] = 0x11U;
    memcpy(&stream[offset], frame, 13U);
    offset = (uint16_t)(offset + 13U);
    memcpy(&stream[offset], &frame[14], REMOTE_IBUS_FRAME_SIZE - 14U);
    offset = (uint16_t)(offset + REMOTE_IBUS_FRAME_SIZE - 14U);
    memcpy(&stream[offset], frame, REMOTE_IBUS_FRAME_SIZE);
    stream[offset + 5U] ^= 0x01U;
    offset = (uint16_t)(offset + REMOTE_IBUS_FRAME_SIZE);
    memcpy(&stream[offset], frame, REMOTE_IBUS_FRAME_SIZE);
    offset = (uint16_t)(offset + REMOTE_IBUS_FRAME_SIZE);

    g_now_ms = 20U;
    RemoteBackend_OnUartRx(1U, stream, offset);
    command = ReadCommand();
    assert(command.frame_sequence == 1U);
    assert(command.channels[0] == 1200U);
    assert(command.channels[9] == 1600U);
}

static void TestInvalidFramesDoNotPublishOrRefresh(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t before;
    RemoteCommand_t after;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    SendIbusFrameAt(10U, channels);
    before = ReadCommand();

    BuildIbusFrame(frame, channels);
    frame[1] = 0x41U;
    g_now_ms = 40U;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
    after = ReadCommand();
    assert(after.frame_sequence == before.frame_sequence);
    assert(after.last_valid_ms == before.last_valid_ms);

    BuildIbusFrame(frame, channels);
    frame[30] ^= 0x01U;
    g_now_ms = 50U;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
    after = ReadCommand();
    assert(after.frame_sequence == before.frame_sequence);
    assert(after.last_valid_ms == before.last_valid_ms);

    channels[config.steering.channel_index] = 899U;
    SendIbusFrameAt(60U, channels);
    after = ReadCommand();
    assert(after.frame_sequence == before.frame_sequence);
    assert(after.last_valid_ms == before.last_valid_ms);
}

static void TestUnmappedChannelsDoNotInvalidateFrame(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    channels[2] = 100U;
    channels[10] = 4095U;
    channels[11] = 42U;
    channels[12] = 3000U;
    channels[13] = 0U;
    SendIbusFrameAt(10U, channels);

    command = ReadCommand();
    assert(command.frame_sequence == 1U);
    assert(command.channels[2] == 100U);
    assert(command.channels[10] == 4095U);
}

static void TestStableOnlineRequiresFrameCountAndDuration(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;
    uint32_t i;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    for (i = 0U; i < 19U; i++)
    {
        SendIbusFrameAt(i * 10U, channels);
    }
    command = ReadCommand();
    assert(!command.stable_online);
    assert(!RemoteBackend_IsConnected());

    InitFlySky(&config);
    for (i = 0U; i < 20U; i++)
    {
        SendIbusFrameAt(i * 5U, channels);
    }
    command = ReadCommand();
    assert(!command.stable_online);

    g_now_ms = 150U;
    RemoteBackend_Process(); /* Waiting alone cannot satisfy reception duration. */
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    command = ReadCommand();
    assert(!command.stable_online);
    SendIbusFrameAt(150U, channels); /* A real frame spans the required interval. */
    command = ReadCommand();
    assert(command.stable_online);
    assert(command.connected);
    assert(RemoteBackend_IsConnected());
}

static void TestInvalidFrameBreaksStabilityStreak(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;
    uint32_t i;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    for (i = 0U; i < 19U; i++)
    {
        SendIbusFrameAt(i * 10U, channels);
    }
    BuildIbusFrame(frame, channels);
    frame[30] ^= 0x01U;
    g_now_ms = 190U;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
    SendIbusFrameAt(200U, channels);
    command = ReadCommand();
    assert(!command.stable_online);
    assert(!RemoteBackend_IsConnected());

    InitFlySky(&config);
    for (i = 0U; i < 19U; i++)
    {
        SendIbusFrameAt(i * 10U, channels);
    }
    BuildIbusFrame(frame, channels);
    frame[1] = 0x41U;
    g_now_ms = 190U;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
    SendIbusFrameAt(200U, channels);
    command = ReadCommand();
    assert(!command.stable_online);
    assert(!RemoteBackend_IsConnected());
}

static void TestTimeoutNeutralizesAndWraparoundIsSafe(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;
    uint32_t i;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    for (i = 0U; i < 20U; i++)
    {
        SendIbusFrameAt(0xFFFFFF00U + (i * 8U), channels);
    }
    g_now_ms = 0xFFFFFF98U;
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    command = ReadCommand();
    assert(command.stable_online);

    g_now_ms = 0xFFFFFF98U + config.timeout_ms - 1U;
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    assert(RemoteBackend_GetCommand(&command));
    assert(command.link_fresh);

    g_now_ms = 0xFFFFFF98U + config.timeout_ms;
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    assert(!RemoteBackend_GetCommand(&command));
    assert(!RemoteBackend_IsConnected());
    assert(command.timeout_latched);
    assert(command.timeout_sequence == 0U); /* Comm has not run yet. */
    RemoteBackend_Process();
    assert(!RemoteBackend_GetCommand(&command));
    assert(command.timeout_sequence == 1U);
    assert(command.steering == 2048U);
    assert(command.throttle == 2048U);
    assert(command.travel_speed_raw == 0U);

    SendIbusFrameAt(g_now_ms + 9U, channels);
    command = ReadCommand();
    assert(!command.stable_online);
    assert(command.timeout_latched);
    assert(command.timeout_sequence == 1U);
}

static void TestProductionDefaultConfig(void)
{
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    assert(config.timeout_ms == REMOTE_IBUS_TIMEOUT_MS);
    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    SendIbusFrameAt(10U, channels);
    command = ReadCommand();

    assert(command.run_enable_configured);
    assert(command.blade_height_configured);
    assert(command.travel_speed_configured);
    assert(command.blade_start_configured);
    assert(command.blade_confirm_configured == !MOWER_GRASS_BAG_CH6_ENABLED);
    assert(!command.remote_run_enable);
    assert(command.height_level == 4U);
    assert(command.blade_speed == 0U);
    assert(!command.run_permitted);
}

static void TestCh4Ch6ConfiguredSemanticsRangeChecks(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    /* Case A: Configured -> Out of range invalidates frame */
    config.blade_start_semantics_configured = true;
    config.blade_confirm_semantics_configured = true;
    InitFlySky(&config);
    SetNormalIbusChannels(channels);

    /* CH4 out of range */
    channels[3] = 3000U;
    SendIbusFrameAt(10U, channels);
    assert(!RemoteBackend_GetCommand(&command));

    channels[3] = 1500U;
    /* CH6 out of range */
    channels[5] = 42U;
    SendIbusFrameAt(20U, channels);
    assert(!RemoteBackend_GetCommand(&command));

    /* Case B: Unconfigured -> Out of range does NOT invalidate frame */
    config.blade_start_semantics_configured = false;
    config.blade_confirm_semantics_configured = false;
    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    channels[3] = 3000U;
    channels[5] = 42U;
    SendIbusFrameAt(30U, channels);
    command = ReadCommand();
    assert(command.frame_sequence == 1U);
    assert(command.channels[3] == 3000U);
    assert(command.channels[5] == 42U);
    assert(!command.blade_start_configured);
    assert(!command.blade_confirm_configured);
}

static void TestCh6ConfirmEventInRemoteBackend(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);

    /* Baseline establishment on startup (CH6 = 1000 us, 3 frames) */
    channels[5] = 1000U;
    SendIbusFrameAt(10U, channels);
    (void)ReadCommand();
    SendIbusFrameAt(20U, channels);
    (void)ReadCommand();
    SendIbusFrameAt(30U, channels);
    command = ReadCommand();
    assert(command.blade_confirm_raw == 1000U);
    assert(!command.has_confirm_event);
    assert(command.confirm_event_sequence == 0U);
    RemoteBackend_SetActiveBladeRequest(7U);

    /* Confirmed edge on CH6 (flip to 2000 us, 3 frames) */
    channels[5] = 2000U;
    SendIbusFrameAt(40U, channels);
    assert(!ReadCommand().has_confirm_event);
    SendIbusFrameAt(50U, channels);
    assert(!ReadCommand().has_confirm_event);
    SendIbusFrameAt(60U, channels);
    command = ReadCommand();
#if MOWER_BLADE_OK_ENABLED
    assert(!command.has_confirm_event);
    assert(command.confirm_event_sequence == 0U);
    assert(command.confirm_request_sequence == 0U);
#else
    assert(command.has_confirm_event);
    assert(command.confirm_event_sequence == 1U);
    assert(command.confirm_request_sequence == 7U);
#endif
    assert(command.blade_confirm_raw == 2000U);

    /* Consume event */
    RemoteBackend_ConsumeConfirmEvent(1U);
    command = ReadCommand();
    assert(!command.has_confirm_event);
#if MOWER_BLADE_OK_ENABLED
    assert(command.confirm_request_sequence == 0U);
#else
    assert(command.confirm_request_sequence == 7U);
#endif
}

static void TestJoystickOutputMapping(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    uint16_t joystick[4];
    RemoteCommand_t command;

    InitFlySky(&config);
    uint32_t t;

    SetNormalIbusChannels(channels);
    channels[0] = 1500U;
    channels[1] = 1500U;
    channels[4] = 1000U;
    channels[7] = 2000U;
    for (t = 10U; t <= 250U; t += 10U)
    {
        SendIbusFrameAt(t, channels);
        RemoteBackend_Advance(t, (RemoteSafetyInput_t){false, false, false, false});
    }

    channels[4] = 2000U;
    for (t = 260U; t <= 280U; t += 10U)
    {
        SendIbusFrameAt(t, channels);
        RemoteBackend_Advance(t, (RemoteSafetyInput_t){false, false, false, false});
    }

    channels[7] = 1000U;
    for (t = 290U; t <= 310U; t += 10U)
    {
        SendIbusFrameAt(t, channels);
        RemoteBackend_Advance(t, (RemoteSafetyInput_t){false, false, false, false});
    }

    channels[7] = 2000U;
    for (t = 320U; t <= 340U; t += 10U)
    {
        SendIbusFrameAt(t, channels);
        RemoteBackend_Advance(t, (RemoteSafetyInput_t){false, false, false, false});
    }

    for (t = 350U; t <= 560U; t += 10U)
    {
        SendIbusFrameAt(t, channels);
        RemoteBackend_Advance(t, (RemoteSafetyInput_t){false, false, false, false});
    }

#if MOWER_BLADE_OK_ENABLED
    channels[3] = 1000U;
    SendIbusFrameAt(560U, channels);
    RemoteBackend_Advance(560U, (RemoteSafetyInput_t){0});
    assert(RemoteBackend_ConfirmUnlock());
#endif
    channels[0] = 1200U;
    channels[1] = 2012U;
    SendIbusFrameAt(570U, channels);
    RemoteBackend_Advance(570U, (RemoteSafetyInput_t){false, false, false, false});

    command = ReadCommand();
    assert(command.run_permitted);
    assert(command.steering != 2048U);
    assert(command.throttle == 4095U);
    assert(command.travel_speed_raw == 1600U);
    assert(command.travel_speed_mps > 1.23f && command.travel_speed_mps < 1.25f);

    RemoteBackend_GetJoystickOutput(joystick);
    assert(joystick[0] == command.steering);
    assert(joystick[1] == command.throttle);
    assert(joystick[2] == command.steering);
    assert(joystick[3] == command.throttle);

    channels[1] = 2011U;
    SendIbusFrameAt(580U, channels);
    assert(ReadCommand().throttle == 4095U);
    RemoteBackend_GetJoystickOutput(joystick);
    assert(joystick[1] == 4095U && joystick[0] != 2048U);

    channels[1] = 988U;
    SendIbusFrameAt(590U, channels);
    assert(ReadCommand().throttle == 0U);
    channels[1] = 987U;
    SendIbusFrameAt(600U, channels);
    assert(ReadCommand().throttle == 0U);
    channels[1] = 989U;
    SendIbusFrameAt(610U, channels);
    assert(ReadCommand().throttle == 0U);

    channels[1] = 1000U;
    SendIbusFrameAt(620U, channels);
    assert(ReadCommand().throttle == 0U);
    RemoteBackend_GetJoystickOutput(joystick);
    assert(joystick[1] == 0U);
    channels[1] = 1250U;
    SendIbusFrameAt(630U, channels);
    assert(ReadCommand().throttle == 2048U);
    channels[1] = 1500U;
    SendIbusFrameAt(640U, channels);
    assert(ReadCommand().throttle == 2048U);
    channels[1] = 1750U;
    SendIbusFrameAt(650U, channels);
    assert(ReadCommand().throttle == 2048U);
    channels[1] = 2000U;
    SendIbusFrameAt(660U, channels);
    assert(ReadCommand().throttle == 4095U);
    channels[1] = 1999U;
    SendIbusFrameAt(670U, channels);
    assert(ReadCommand().throttle == 2048U);
    channels[1] = 1001U;
    SendIbusFrameAt(680U, channels);
    assert(ReadCommand().throttle == 2048U);
}

static void TestAxisCalibrationAndReversal(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    FlySkyIbusServo_t ibus;
    FlySkyIbusSnapshot_t snapshot;
    config.steering.reversed = false;
    config.throttle.min_us = 1100U;
    config.throttle.center_us = 1450U;
    config.throttle.max_us = 1900U;
    config.throttle.reversed = true;
    FlySkyIbusServo_Init(&ibus, &config);
    SetNormalIbusChannels(channels);
    channels[1] = 1100U;
    BuildIbusFrame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 10U);
    assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
    assert(snapshot.throttle == 4095U);
    channels[1] = 1450U;
    BuildIbusFrame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 20U);
    assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
    assert(snapshot.throttle >= 2047U && snapshot.throttle <= 2048U);
    channels[1] = 1900U;
    BuildIbusFrame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 30U);
    assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
    assert(snapshot.throttle == 0U);
    // A small displacement must not produce a full-scale command.
    channels[1] = 1400U;
    channels[0] = 1550U;
    BuildIbusFrame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 40U);
    assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
    assert(snapshot.throttle == 2048U);
    assert(snapshot.steering > 2048U && snapshot.steering < 2300U);
    // Interior values must stay neutral, including single-microsecond edges.
    for (uint16_t pulse = 1101U; pulse < 1900U; pulse++)
    {
        channels[1] = pulse;
        BuildIbusFrame(frame, channels);
        FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 50U + pulse);
        assert(FlySkyIbusServo_ReadSnapshot(&ibus, &snapshot));
        assert(snapshot.throttle == 2048U);
    }

}

static void TestTravelSpeedMapping(void)
{
    assert(RemoteBackend_MapTravelSpeed(1000U) == 0.4f);
    assert(RemoteBackend_MapTravelSpeed(2000U) == 1.8f);
    assert(RemoteBackend_MapTravelSpeed(900U) == 0.4f);
    assert(RemoteBackend_MapTravelSpeed(2100U) == 1.8f);
    assert(RemoteBackend_MapTravelSpeed(1500U) > 1.09f);
    assert(RemoteBackend_MapTravelSpeed(1500U) < 1.11f);
    for (uint16_t pulse = 1001U; pulse <= 2000U; pulse++)
        assert(RemoteBackend_MapTravelSpeed(pulse) > RemoteBackend_MapTravelSpeed(pulse - 1U));
    assert(RemoteBackend_ApplyReverseSpeed(1.0f, true) == 0.8f);
    assert(RemoteBackend_ApplyReverseSpeed(1.0f, false) == 1.0f);
}

static void TestBladeHeightKnobMapping(void)
{
    FlySkyIbusConfig_t config = TestConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteCommand_t command;

    InitFlySky(&config);
    SetNormalIbusChannels(channels);

    /* Level 1: CCW end (~1000 us) */
    channels[8] = 988U;
    SendIbusFrameAt(10U, channels);
    command = ReadCommand();
    assert(command.height_level == 1U);

    channels[8] = 1100U;
    SendIbusFrameAt(20U, channels);
    command = ReadCommand();
    assert(command.height_level == 1U);

    /* Transition from 1 to 2: threshold is 1200, hyst is 25 -> must exceed 1225 */
    channels[8] = 1210U;
    SendIbusFrameAt(30U, channels);
    command = ReadCommand();
    assert(command.height_level == 1U); /* Hysteresis holds at 1 */

    channels[8] = 1230U;
    SendIbusFrameAt(40U, channels);
    command = ReadCommand();
    assert(command.height_level == 2U);

    /* Transition from 2 back to 1: must drop below 1200 - 25 = 1175 */
    channels[8] = 1190U;
    SendIbusFrameAt(50U, channels);
    command = ReadCommand();
    assert(command.height_level == 2U); /* Hysteresis holds at 2 */

    channels[8] = 1170U;
    SendIbusFrameAt(60U, channels);
    command = ReadCommand();
    assert(command.height_level == 1U);

    /* Level 3: Center (~1500 us) */
    channels[8] = 1500U;
    SendIbusFrameAt(70U, channels);
    command = ReadCommand();
    assert(command.height_level == 3U);

    /* Level 4: ~1700 us */
    channels[8] = 1700U;
    SendIbusFrameAt(80U, channels);
    command = ReadCommand();
    assert(command.height_level == 4U);

    /* Level 5: CW end (~2000 us) */
    channels[8] = 2012U;
    SendIbusFrameAt(90U, channels);
    command = ReadCommand();
    assert(command.height_level == 5U);

    /* Test disabled semantics */
    config.blade_height_semantics_configured = false;
    InitFlySky(&config);
    channels[8] = 1500U;
    SendIbusFrameAt(100U, channels);
    command = ReadCommand();
    assert(!command.blade_height_configured);
    assert(command.height_level == 0U);
}
#endif

#if MOWER_GRASS_BAG_CH6_ENABLED
static void TestGrassBagCh6Release(void)
{
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    GrassBagTarget_t target = GRASS_BAG_TARGET_UNKNOWN;
    RemoteCommand_t command;
    assert(config.grass_bag_semantics_configured);
    assert(!config.blade_confirm_semantics_configured);
    assert(config.grass_bag_index == 5U);
    InitFlySky(&config);
    SetNormalIbusChannels(channels);
    channels[0] = channels[1] = 1500U;
    for (uint32_t t = 10U; t <= 250U; t += 10U) {
        SendIbusFrameAt(t, channels);
        command = ReadCommand();
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
#if MOWER_BLADE_OK_ENABLED
    channels[3] = 1000U;
    for (uint32_t t = 260U; t <= 530U; t += 10U) { SendIbusFrameAt(t, channels); (void)ReadCommand(); }
    assert(RemoteBackend_ConfirmUnlock());
    SendIbusFrameAt(540U, channels); (void)ReadCommand();
#else
    /* Real authorization sequence: CH8 high -> low -> high, sticks centered. */
    channels[7] = 1000U;
    for (uint32_t t = 260U; t <= 280U; t += 10U) { SendIbusFrameAt(t, channels); (void)ReadCommand(); }
    channels[7] = 2000U;
    for (uint32_t t = 290U; t <= 540U; t += 10U) { SendIbusFrameAt(t, channels); (void)ReadCommand(); }
#endif
    assert(ReadCommand().run_permitted);
    /* Baseline requires three NEW frames even after authorization. */
    for (uint32_t t = 550U; t <= 570U; t += 10U) {
        SendIbusFrameAt(t, channels);
        command = ReadCommand();
        assert(command.grass_bag_raw == 1000U);
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
    assert(ReadCommand().grass_bag_target == GRASS_BAG_TARGET_OPEN);
    channels[5] = 2000U;
    for (uint32_t t = 580U; t <= 600U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(RemoteBackend_AdvanceGrassBag(true, &target) == (t == 600U));
        assert(!RemoteBackend_AdvanceGrassBag(true, &target)); /* duplicate read */
        assert(!ReadCommand().has_confirm_event);
    }
    assert(target == GRASS_BAG_TARGET_CLOSED);
    channels[5] = 1000U;
    for (uint32_t t = 610U; t <= 630U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(RemoteBackend_AdvanceGrassBag(true, &target) == (t == 630U));
    }
    assert(target == GRASS_BAG_TARGET_OPEN);
    /* Connection/mode loss supplied by Motion discards the edge and baseline. */
    assert(!RemoteBackend_AdvanceGrassBag(false, &target));
    channels[5] = 2000U;
    for (uint32_t t = 640U; t <= 680U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
    /* Out-of-range CH6 rejects the whole frame, including timestamp refresh. */
    command = ReadCommand();
    channels[5] = 899U;
    SendIbusFrameAt(690U, channels);
    assert(ReadCommand().frame_sequence == command.frame_sequence);
    uint32_t reconnect_ms = command.last_valid_ms + config.timeout_ms + 20U;
    g_now_ms = command.last_valid_ms + config.timeout_ms;
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    (void)RemoteBackend_GetCommand(&command);
    assert(!command.link_fresh);
    assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    (void)RemoteBackend_GetCommand(&command);
    assert(command.grass_bag_target == GRASS_BAG_TARGET_UNKNOWN);
    channels[5] = 1000U;
    channels[3] = 1500U;
    for (uint32_t t = reconnect_ms; t <= reconnect_ms + 240U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
    assert(!ReadCommand().run_permitted); /* reconnect alone does not unlock */
    channels[7] = 1000U;
    for (uint32_t t = reconnect_ms + 250U; t <= reconnect_ms + 270U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
    channels[7] = 2000U;
#if MOWER_BLADE_OK_ENABLED
    channels[3] = 1000U;
#endif
    for (uint32_t t = reconnect_ms + 280U; t <= reconnect_ms + 540U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
#if MOWER_BLADE_OK_ENABLED
        if (t == reconnect_ms + 480U) assert(RemoteBackend_ConfirmUnlock());
#endif
        assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    }
    assert(ReadCommand().run_permitted);
    assert(ReadCommand().grass_bag_target == GRASS_BAG_TARGET_OPEN);
    channels[5] = 2000U;
    for (uint32_t t = reconnect_ms + 550U; t <= reconnect_ms + 570U; t += 10U) {
        SendIbusFrameAt(t, channels); (void)ReadCommand();
        assert(RemoteBackend_AdvanceGrassBag(true, &target) == (t == reconnect_ms + 570U));
    }
    /* A real safety input overrides the caller's permission and resets baseline. */
    RemoteBackend_Advance(reconnect_ms + 570U, (RemoteSafetyInput_t){false, true, false, false});
    assert(!ReadCommand().run_permitted);
    assert(!RemoteBackend_AdvanceGrassBag(true, &target));
    assert(ReadCommand().grass_bag_target == GRASS_BAG_TARGET_UNKNOWN);
}
#endif

int main(void)
{
#if REMOTE_BACKEND_A28_E34
    RemoteBackendClock_t clock = {HarnessNow, NULL};
    RemoteCommand_t command;
    const uint8_t stick_frame[] = {0xABU, 0x08U, 0x00U, 0x07U, 0x00U,
                                   0x09U, 0x00U, 0x08U, 0x00U, 0xBAU};

    RemoteBackend_Init(clock);
    g_now_ms = 25U;
    RemoteBackend_OnUartRx(3U, stick_frame, sizeof(stick_frame));
    assert(RemoteBackend_GetCommand(&command));
    assert(command.channels[0] == 2048U);
    assert(command.channels[1] == 1792U);
    assert(command.channels[2] == 2304U);
    assert(command.connected);
    assert(RemoteBackend_LegacyTelemetryEnabled());
    assert(!RemoteBackend_UsesUart(1U));
    g_now_ms = 325U;
    RemoteBackend_Advance(g_now_ms, (RemoteSafetyInput_t){false, false, false, false});
    assert(!RemoteBackend_IsConnected());
    uint16_t legacy_joystick[4];
    RemoteBackend_GetJoystickOutput(legacy_joystick);
    assert(legacy_joystick[0] == command.channels[0]);
    assert(legacy_joystick[1] == command.channels[1]);
    assert(legacy_joystick[2] == command.channels[2]);
    assert(legacy_joystick[3] == command.channels[3]);
#elif REMOTE_BACKEND_FLYSKY_IBUS
    assert(RemoteBackend_UsesUart(1U));
    assert(!RemoteBackend_UsesUart(3U));
    assert(!RemoteBackend_LegacyTelemetryEnabled());
    TestValidSplitFramePublishesSnapshot();
    TestBladeGearUsesCh7NotCh10();
    TestNoiseDroppedByteAndBadFrameResynchronize();
    TestInvalidFramesDoNotPublishOrRefresh();
    TestUnmappedChannelsDoNotInvalidateFrame();
    TestStableOnlineRequiresFrameCountAndDuration();
    TestInvalidFrameBreaksStabilityStreak();
    TestTimeoutNeutralizesAndWraparoundIsSafe();
    TestProductionDefaultConfig();
    TestJoystickOutputMapping();
    TestTravelSpeedMapping();
    TestAxisCalibrationAndReversal();
    TestBladeHeightKnobMapping();
    TestCh4Ch6ConfiguredSemanticsRangeChecks();
    TestCh6ConfirmEventInRemoteBackend();
#if MOWER_GRASS_BAG_CH6_ENABLED
    TestGrassBagCh6Release();
#endif
#endif
    return 0;
}
