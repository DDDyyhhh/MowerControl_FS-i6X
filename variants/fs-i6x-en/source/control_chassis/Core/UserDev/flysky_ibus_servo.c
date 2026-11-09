#include "flysky_ibus_servo.h"

#include "../../../experiments/ticket03/exp3_board.h"

#include <limits.h>
#include <string.h>

#define IBUS_LENGTH_BYTE  0x20U
#define IBUS_COMMAND_BYTE 0x40U
#define REMOTE_AXIS_CENTER 2048U
#define REMOTE_AXIS_MAX    4095U

static bool FlySkyIbusServo_AxisConfigValid(const FlySkyIbusAxisConfig_t *axis)
{
    return (axis->channel_index < REMOTE_IBUS_CHANNEL_COUNT) &&
           (axis->min_us < axis->center_us) &&
           (axis->center_us < axis->max_us);
}

static bool FlySkyIbusServo_ConfigValid(const FlySkyIbusConfig_t *config)
{
    if ((config == NULL) ||
        !FlySkyIbusServo_AxisConfigValid(&config->steering) ||
        !FlySkyIbusServo_AxisConfigValid(&config->throttle) ||
        (config->blade_start_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->run_enable_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->blade_confirm_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->grass_bag_semantics_configured &&
         ((config->grass_bag_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
          !MOWER_GRASS_BAG_CH6_ENABLED || config->blade_confirm_semantics_configured)) ||
        (config->blade_gear_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->unlock_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->blade_height_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->travel_speed_index >= REMOTE_IBUS_CHANNEL_COUNT) ||
        (config->valid_min_us > config->valid_max_us) ||
        (config->timeout_ms == 0U) ||
        (config->stable_min_frames == 0U))
    {
        return false;
    }

    if (config->blade_height_semantics_configured)
    {
        uint16_t t1 = (config->blade_height_t1_us != 0U) ? config->blade_height_t1_us : REMOTE_BLADE_HEIGHT_T1_US;
        uint16_t t2 = (config->blade_height_t2_us != 0U) ? config->blade_height_t2_us : REMOTE_BLADE_HEIGHT_T2_US;
        uint16_t t3 = (config->blade_height_t3_us != 0U) ? config->blade_height_t3_us : REMOTE_BLADE_HEIGHT_T3_US;
        uint16_t t4 = (config->blade_height_t4_us != 0U) ? config->blade_height_t4_us : REMOTE_BLADE_HEIGHT_T4_US;

        if (!((t1 < t2) && (t2 < t3) && (t3 < t4)))
        {
            return false;
        }
    }

    return true;
}

static uint16_t FlySkyIbusServo_MapAxis(uint16_t value,
                                        const FlySkyIbusAxisConfig_t *config)
{
    uint32_t scaled;

    if (value <= config->min_us)
    {
        scaled = 0U;
    }
    else if (value < config->center_us)
    {
        scaled = ((uint32_t)(value - config->min_us) * REMOTE_AXIS_CENTER) /
                 (uint32_t)(config->center_us - config->min_us);
    }
    else if (value < config->max_us)
    {
        scaled = REMOTE_AXIS_CENTER +
                 (((uint32_t)(value - config->center_us) *
                   (REMOTE_AXIS_MAX - REMOTE_AXIS_CENTER)) /
                  (uint32_t)(config->max_us - config->center_us));
    }
    else
    {
        scaled = REMOTE_AXIS_MAX;
    }

    if (config->reversed)
    {
        scaled = REMOTE_AXIS_MAX - scaled;
    }
    return (uint16_t)scaled;
}

static bool FlySkyIbusServo_ChecksumValid(const uint8_t *frame)
{
    uint16_t checksum;
    uint16_t expected;
    uint16_t sum = 0U;
    uint8_t i;

    for (i = 0U; i < 30U; i++)
    {
        sum = (uint16_t)(sum + frame[i]);
    }
    expected = (uint16_t)(0xFFFFU - sum);
    checksum = (uint16_t)((uint16_t)frame[30] | ((uint16_t)frame[31] << 8));
    return checksum == expected;
}

static void FlySkyIbusServo_Decode(const uint8_t *frame,
                                   uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT])
{
    uint8_t i;

    for (i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; i++)
    {
        channels[i] = (uint16_t)((uint16_t)frame[2U + (2U * i)] |
                                 ((uint16_t)frame[3U + (2U * i)] << 8));
    }
}

static bool FlySkyIbusServo_MappedChannelsValid(
    const FlySkyIbusServo_t *ibus,
    const uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT])
{
    uint8_t mapped[10];
    uint8_t count = 0U;

    mapped[count++] = ibus->config.steering.channel_index;
    mapped[count++] = ibus->config.throttle.channel_index;
    mapped[count++] = ibus->config.run_enable_index;
    mapped[count++] = ibus->config.blade_gear_index;
    mapped[count++] = ibus->config.unlock_index;
    mapped[count++] = ibus->config.blade_height_index;
    mapped[count++] = ibus->config.travel_speed_index;

    if (ibus->config.blade_start_semantics_configured)
    {
        mapped[count++] = ibus->config.blade_start_index;
    }
    if (ibus->config.blade_confirm_semantics_configured)
    {
        mapped[count++] = ibus->config.blade_confirm_index;
    }
    if (ibus->config.grass_bag_semantics_configured)
    {
        mapped[count++] = ibus->config.grass_bag_index;
    }
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        uint16_t value = channels[mapped[i]];
        if ((value < ibus->config.valid_min_us) ||
            (value > ibus->config.valid_max_us))
        {
            return false;
        }
    }
    return true;
}

static void FlySkyIbusServo_Publish(FlySkyIbusServo_t *ibus,
                                    const FlySkyIbusSnapshot_t *snapshot)
{
    ibus->snapshot = *snapshot;
    ibus->snapshot_available = 1U;
}

static void FlySkyIbusServo_UpdateStableState(FlySkyIbusServo_t *ibus,
                                              FlySkyIbusSnapshot_t *snapshot,
                                              uint32_t now_ms)
{
    if (!snapshot->stable_online &&
        (ibus->consecutive_valid_frames >= ibus->config.stable_min_frames) &&
        ((uint32_t)(now_ms - ibus->stability_started_ms) >=
         ibus->config.stable_min_ms))
    {
        snapshot->stable_online = true;
    }
}

static void FlySkyIbusServo_BreakStabilityStreak(FlySkyIbusServo_t *ibus)
{
    FlySkyIbusSnapshot_t snapshot;

    /* The streak admits a new link. Once admitted, rejected bytes do not
     * renew freshness; the valid-frame watchdog decides when it goes offline. */
    if ((ibus->snapshot_available != 0U) &&
        ibus->snapshot.stable_online && (ibus->timed_out == 0U))
    {
        return;
    }
    ibus->consecutive_valid_frames = 0U;
    ibus->stability_started_ms = 0U;
    if ((ibus->snapshot_available != 0U) &&
        !ibus->snapshot.stable_online)
    {
        snapshot = ibus->snapshot;
        snapshot.stable_online = false;
        FlySkyIbusServo_Publish(ibus, &snapshot);
    }
}

#if MOWER_BLADE_OK_ENABLED
static void FlySkyIbusServo_TrackCh5(FlySkyIbusServo_t *ibus,
                                    FlySkyIbusSnapshot_t *snapshot)
{
    const uint16_t raw = snapshot->run_enable_raw;
    const uint16_t low = ibus->config.run_enable_off_us;
    const uint16_t high = ibus->config.run_enable_on_us;
    const uint16_t low_diff = raw > low ? raw - low : low - raw;
    const uint16_t high_diff = raw > high ? raw - high : high - raw;
    uint8_t position = 0U; /* Unknown, low, high. Unknown also fails closed. */

    if (ibus->config.run_enable_semantics_configured)
    {
        if (low_diff <= ibus->config.switch_position_tolerance_us)
            position = 1U;
        else if (high_diff <= ibus->config.switch_position_tolerance_us)
            position = 2U;
    }
    if (ibus->ch5_baseline_valid && position != ibus->ch5_position)
        ++snapshot->ch5_stop_sequence;
    ibus->ch5_position = position;
    ibus->ch5_baseline_valid = true;
}
#endif

static bool FlySkyIbusServo_HandleFrame(FlySkyIbusServo_t *ibus,
                                        const uint8_t *frame,
                                        uint32_t received_at_ms, uint32_t now_ms)
{
    FlySkyIbusSnapshot_t snapshot;
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];

    if ((frame[0] != IBUS_LENGTH_BYTE) ||
        (frame[1] != IBUS_COMMAND_BYTE) ||
        !FlySkyIbusServo_ChecksumValid(frame))
    {
        if (ibus->crc_errors != UINT32_MAX) ++ibus->crc_errors;
        return false;
    }

    FlySkyIbusServo_Decode(frame, channels);
    if (!FlySkyIbusServo_MappedChannelsValid(ibus, channels))
    {
        if (ibus->invalid_channel_frames != UINT32_MAX) ++ibus->invalid_channel_frames;
        return false;
    }
#if EXP3_ENABLED
    /* Authorization/session deadlines stay on the current processing clock. */
    exp3_board_channels(now_ms, channels[3], channels[10], channels[11]);
#else
    (void)now_ms;
#endif

    if (ibus->snapshot_available != 0U)
    {
        snapshot = ibus->snapshot;
    }
    else
    {
        memset(&snapshot, 0, sizeof(snapshot));
    }

    memcpy(snapshot.channels, channels, sizeof(channels));
    snapshot.steering = FlySkyIbusServo_MapAxis(
        channels[ibus->config.steering.channel_index], &ibus->config.steering);
    // CH2 selects direction only at calibrated endpoints; the entire interior
    // is neutral. CH10 alone selects travel speed (no throttle multiplication).
    const FlySkyIbusAxisConfig_t *throttle = &ibus->config.throttle;
    uint16_t throttle_us = channels[throttle->channel_index];
    snapshot.throttle = REMOTE_AXIS_CENTER;
    if (throttle_us <= throttle->min_us)
        snapshot.throttle = throttle->reversed ? REMOTE_AXIS_MAX : 0U;
    else if (throttle_us >= throttle->max_us)
        snapshot.throttle = throttle->reversed ? 0U : REMOTE_AXIS_MAX;
    snapshot.blade_start_raw = channels[ibus->config.blade_start_index];
    snapshot.blade_confirm_raw = channels[ibus->config.blade_confirm_index];
    snapshot.grass_bag_raw = channels[ibus->config.grass_bag_index];
    snapshot.grass_bag_configured = ibus->config.grass_bag_semantics_configured;
    snapshot.run_enable_raw = channels[ibus->config.run_enable_index];
#if MOWER_BLADE_OK_ENABLED
    FlySkyIbusServo_TrackCh5(ibus, &snapshot);
#endif
    snapshot.blade_gear_raw = channels[ibus->config.blade_gear_index];
    snapshot.unlock_raw = channels[ibus->config.unlock_index];
    snapshot.blade_height_raw = channels[ibus->config.blade_height_index];
    snapshot.travel_speed_raw = channels[ibus->config.travel_speed_index];
    snapshot.run_enable_configured =
        ibus->config.run_enable_semantics_configured;
    snapshot.blade_start_configured =
        ibus->config.blade_start_semantics_configured;
    snapshot.blade_confirm_configured =
        ibus->config.blade_confirm_semantics_configured;
    snapshot.blade_height_configured =
        ibus->config.blade_height_semantics_configured;
    snapshot.travel_speed_configured =
        ibus->config.travel_speed_semantics_configured;
    snapshot.link_fresh = true;
    snapshot.timeout_latched = snapshot.timeout_latched || (ibus->timed_out != 0U);
    snapshot.last_valid_ms = received_at_ms;
    snapshot.frame_sequence++;

    if ((ibus->consecutive_valid_frames == 0U) || (ibus->timed_out != 0U))
    {
        ibus->stability_started_ms = received_at_ms;
        ibus->consecutive_valid_frames = 1U;
        snapshot.stable_online = false;
    }
    else if (ibus->consecutive_valid_frames < UINT8_MAX)
    {
        ibus->consecutive_valid_frames++;
    }
    ibus->timed_out = 0U;
    FlySkyIbusServo_UpdateStableState(ibus, &snapshot, received_at_ms);
    FlySkyIbusServo_Publish(ibus, &snapshot);
    return true;
}

static void FlySkyIbusServo_DiscardFirstByte(FlySkyIbusServo_t *ibus)
{
    if (ibus->stream_count > 1U)
    {
        memmove(ibus->stream, &ibus->stream[1], (size_t)(ibus->stream_count - 1U));
        memmove(ibus->stream_received_ms, &ibus->stream_received_ms[1],
                (size_t)(ibus->stream_count - 1U) * sizeof(ibus->stream_received_ms[0]));
    }
    if (ibus->stream_count != 0U)
    {
        ibus->stream_count--;
    }
}

static void FlySkyIbusServo_ProcessStream(FlySkyIbusServo_t *ibus, uint32_t now_ms)
{
    for (;;)
    {
        while ((ibus->stream_count != 0U) &&
               (ibus->stream[0] != IBUS_LENGTH_BYTE))
        {
            FlySkyIbusServo_DiscardFirstByte(ibus);
        }

        if (ibus->stream_count < 2U)
        {
            return;
        }
        if (ibus->stream[1] != IBUS_COMMAND_BYTE)
        {
            FlySkyIbusServo_BreakStabilityStreak(ibus);
            FlySkyIbusServo_DiscardFirstByte(ibus);
            continue;
        }
        if (ibus->stream_count < REMOTE_IBUS_FRAME_SIZE)
        {
            return;
        }
        if (FlySkyIbusServo_HandleFrame(ibus, ibus->stream,
                ibus->stream_received_ms[REMOTE_IBUS_FRAME_SIZE - 1U], now_ms))
        {
            ibus->stream_count = 0U;
            return;
        }
        FlySkyIbusServo_BreakStabilityStreak(ibus);
        FlySkyIbusServo_DiscardFirstByte(ibus);
    }
}

FlySkyIbusConfig_t FlySkyIbusServo_DefaultConfig(void)
{
    FlySkyIbusConfig_t config;

    memset(&config, 0, sizeof(config));
    config.steering.channel_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_STEERING);
    config.steering.min_us = REMOTE_IBUS_CAL_MIN_US;
    config.steering.center_us = REMOTE_IBUS_CAL_CENTER_US;
    config.steering.max_us = REMOTE_IBUS_CAL_MAX_US;
    config.steering.reversed = true;
    config.throttle.channel_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_THROTTLE);
    config.throttle.min_us = REMOTE_IBUS_CAL_MIN_US;
    config.throttle.center_us = REMOTE_IBUS_CAL_CENTER_US;
    config.throttle.max_us = REMOTE_IBUS_CAL_MAX_US;
    config.run_enable_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_RUN_ENABLE);
    config.blade_gear_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_BLADE_GEAR);
    config.unlock_index = REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_UNLOCK);
    config.blade_height_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_BLADE_HEIGHT);
    config.travel_speed_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_TRAVEL_SPEED);
    config.blade_start_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_BLADE_START);
    config.blade_confirm_index =
        REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_BLADE_CONFIRM);
    config.blade_start_center_us = REMOTE_BLADE_START_CENTER_US;
    config.blade_start_start_us = REMOTE_BLADE_START_START_US;
    config.blade_start_tolerance_us = REMOTE_BLADE_START_TOLERANCE_US;
    config.blade_confirm_low_us = REMOTE_BLADE_CONFIRM_LOW_US;
    config.blade_confirm_high_us = REMOTE_BLADE_CONFIRM_HIGH_US;
    config.blade_confirm_tolerance_us = REMOTE_BLADE_CONFIRM_TOLERANCE_US;
    config.blade_confirm_frames = REMOTE_BLADE_CONFIRM_FRAMES;
    config.valid_min_us = REMOTE_IBUS_CHANNEL_VALID_MIN_US;
    config.valid_max_us = REMOTE_IBUS_CHANNEL_VALID_MAX_US;
    config.timeout_ms = REMOTE_IBUS_TIMEOUT_MS;
    config.stable_min_ms = REMOTE_IBUS_STABLE_MIN_MS;
    config.stable_min_frames = REMOTE_IBUS_STABLE_MIN_FRAMES;
    config.run_enable_off_us = REMOTE_IBUS_CAL_MIN_US;
    config.run_enable_on_us = REMOTE_IBUS_CAL_MAX_US;
    config.unlock_up_us = REMOTE_IBUS_CAL_MIN_US;
    config.unlock_middle_us = REMOTE_IBUS_CAL_CENTER_US;
    config.unlock_down_us = REMOTE_IBUS_CAL_MAX_US;
    config.blade_gear_low_us = REMOTE_IBUS_CAL_MAX_US;
    config.blade_gear_medium_us = REMOTE_IBUS_CAL_CENTER_US;
    config.blade_gear_high_us = REMOTE_IBUS_CAL_MIN_US;
    config.switch_position_tolerance_us = REMOTE_SWITCH_POSITION_TOLERANCE_US;
    config.stick_center_tolerance_us = REMOTE_STICK_CENTER_TOLERANCE_US;
    config.stick_center_hold_ms = REMOTE_STICK_CENTER_HOLD_MS;
    config.unlock_window_ms = REMOTE_UNLOCK_WINDOW_MS;
    config.switch_confirm_frames = REMOTE_SWITCH_CONFIRM_FRAMES;
    config.allow_unlock_direct_jump = false;
    config.run_enable_semantics_configured = true;
    config.blade_height_t1_us = REMOTE_BLADE_HEIGHT_T1_US;
    config.blade_height_t2_us = REMOTE_BLADE_HEIGHT_T2_US;
    config.blade_height_t3_us = REMOTE_BLADE_HEIGHT_T3_US;
    config.blade_height_t4_us = REMOTE_BLADE_HEIGHT_T4_US;
    config.blade_height_hysteresis_us = REMOTE_BLADE_HEIGHT_HYSTERESIS_US;
    config.blade_height_semantics_configured = true;
    config.blade_start_semantics_configured = true;
    config.blade_confirm_semantics_configured = !MOWER_GRASS_BAG_CH6_ENABLED;
    config.grass_bag_index = REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_GRASS_BAG);
    config.grass_bag_semantics_configured = MOWER_GRASS_BAG_CH6_ENABLED != 0;
    config.travel_speed_semantics_configured = true;
    return config;
}

void FlySkyIbusServo_Init(FlySkyIbusServo_t *ibus, const FlySkyIbusConfig_t *config)
{
    FlySkyIbusConfig_t selected;

    if (ibus == NULL)
    {
        return;
    }
    selected = (config != NULL) ? *config : FlySkyIbusServo_DefaultConfig();
    if (!FlySkyIbusServo_ConfigValid(&selected))
    {
        selected = FlySkyIbusServo_DefaultConfig();
    }
    memset(ibus, 0, sizeof(*ibus));
    ibus->config = selected;
}

bool FlySkyIbusServo_ExpireStream(FlySkyIbusServo_t *ibus, uint32_t now_ms)
{
    if (ibus != NULL && ibus->stream_count != 0U &&
        (uint32_t)(now_ms - ibus->stream_received_ms[0]) >= ibus->config.timeout_ms)
    {
        FlySkyIbusServo_ResetStream(ibus);
        return true;
    }
    return false;
}

void FlySkyIbusServo_FeedReceived(FlySkyIbusServo_t *ibus, const uint8_t *data,
                                 uint16_t length, uint32_t received_at_ms, uint32_t now_ms)
{
    if (ibus == NULL || data == NULL) return;
    (void)FlySkyIbusServo_ExpireStream(ibus, now_ms);
    if ((uint32_t)(now_ms - received_at_ms) >= ibus->config.timeout_ms) return;
    for (uint16_t i = 0U; i < length; ++i)
    {
        if (ibus->stream_count == REMOTE_IBUS_FRAME_SIZE)
        {
            FlySkyIbusServo_ProcessStream(ibus, now_ms);
            if (ibus->stream_count == REMOTE_IBUS_FRAME_SIZE)
                FlySkyIbusServo_DiscardFirstByte(ibus);
        }
        ibus->stream_received_ms[ibus->stream_count] = received_at_ms;
        ibus->stream[ibus->stream_count++] = data[i];
        FlySkyIbusServo_ProcessStream(ibus, now_ms);
    }
}

void FlySkyIbusServo_Feed(FlySkyIbusServo_t *ibus, const uint8_t *data,
                          uint16_t length, uint32_t now_ms)
{
    FlySkyIbusServo_FeedReceived(ibus, data, length, now_ms, now_ms);
}

void FlySkyIbusServo_Advance(FlySkyIbusServo_t *ibus, uint32_t now_ms)
{
    FlySkyIbusSnapshot_t snapshot;

    if ((ibus == NULL) || (ibus->snapshot_available == 0U))
    {
        return;
    }

    snapshot = ibus->snapshot;
    if (snapshot.link_fresh &&
        ((uint32_t)(now_ms - snapshot.last_valid_ms) >= ibus->config.timeout_ms))
    {
        snapshot.link_fresh = false;
        snapshot.stable_online = false;
        snapshot.timeout_latched = true;
        snapshot.timeout_sequence++;
        snapshot.steering = REMOTE_AXIS_CENTER;
        snapshot.throttle = REMOTE_AXIS_CENTER;
        snapshot.travel_speed_raw = 0U;
        ibus->consecutive_valid_frames = 0U;
        ibus->timed_out = 1U;
        ibus->ch5_baseline_valid = false;
        FlySkyIbusServo_Publish(ibus, &snapshot);
        return;
    }

    FlySkyIbusServo_UpdateStableState(ibus, &snapshot, snapshot.last_valid_ms);
    if (snapshot.stable_online !=
        ibus->snapshot.stable_online)
    {
        FlySkyIbusServo_Publish(ibus, &snapshot);
    }
}

void FlySkyIbusServo_ResetStream(FlySkyIbusServo_t *ibus)
{
    if (ibus != NULL)
    {
        ibus->stream_count = 0U;
        FlySkyIbusServo_BreakStabilityStreak(ibus);
    }
}

bool FlySkyIbusServo_ReadSnapshot(const FlySkyIbusServo_t *ibus,
                                  FlySkyIbusSnapshot_t *snapshot)
{
    if ((ibus == NULL) || (snapshot == NULL) || (ibus->snapshot_available == 0U))
    {
        return false;
    }

    *snapshot = ibus->snapshot;
    return true;
}

bool FlySkyIbusServo_IsStableOnline(const FlySkyIbusServo_t *ibus)
{
    FlySkyIbusSnapshot_t snapshot;

    return FlySkyIbusServo_ReadSnapshot(ibus, &snapshot) &&
           snapshot.stable_online && snapshot.link_fresh;
}

uint32_t FlySkyIbusServo_LastReceiveAgeMs(const FlySkyIbusServo_t *ibus,
                                          uint32_t now_ms)
{
    FlySkyIbusSnapshot_t snapshot;

    if (!FlySkyIbusServo_ReadSnapshot(ibus, &snapshot))
    {
        return UINT32_MAX;
    }
    return (uint32_t)(now_ms - snapshot.last_valid_ms);
}
