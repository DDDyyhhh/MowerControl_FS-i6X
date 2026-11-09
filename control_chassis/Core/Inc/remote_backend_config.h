#ifndef REMOTE_BACKEND_CONFIG_H
#define REMOTE_BACKEND_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Build system must define exactly one selector to 1 and the other to 0. */
#ifndef REMOTE_BACKEND_A28_E34
#error "Define REMOTE_BACKEND_A28_E34 as 0 or 1"
#endif

#ifndef REMOTE_BACKEND_FLYSKY_IBUS
#error "Define REMOTE_BACKEND_FLYSKY_IBUS as 0 or 1"
#endif

#if ((REMOTE_BACKEND_A28_E34 != 0) && (REMOTE_BACKEND_A28_E34 != 1))
#error "REMOTE_BACKEND_A28_E34 must be 0 or 1"
#endif

#if ((REMOTE_BACKEND_FLYSKY_IBUS != 0) && (REMOTE_BACKEND_FLYSKY_IBUS != 1))
#error "REMOTE_BACKEND_FLYSKY_IBUS must be 0 or 1"
#endif

#if ((REMOTE_BACKEND_A28_E34 + REMOTE_BACKEND_FLYSKY_IBUS) != 1)
#error "Select exactly one Remote Backend: A28/E34 or FlySky i-BUS"
#endif

#ifndef MOWER_GRASS_BAG_CH6_ENABLED
#define MOWER_GRASS_BAG_CH6_ENABLED 0
#endif
#if MOWER_GRASS_BAG_CH6_ENABLED && (!REMOTE_BACKEND_FLYSKY_IBUS || !MOWER_BLADE_OK_ENABLED)
#error "CH6 grass bag requires FlySky and OK blade authorization"
#endif
#define REMOTE_IBUS_CHANNEL_GRASS_BAG 6U

#define REMOTE_IBUS_FRAME_SIZE            32U
#define REMOTE_IBUS_CHANNEL_COUNT         14U
#define REMOTE_IBUS_CHANNEL_VALID_MIN_US  900U
#define REMOTE_IBUS_CHANNEL_VALID_MAX_US  2100U
#define REMOTE_IBUS_CAL_MIN_US            1000U
#define REMOTE_IBUS_CAL_CENTER_US         1500U
#define REMOTE_IBUS_CAL_MAX_US            2000U
#define REMOTE_TRAVEL_SPEED_MIN_MPS         0.4f
#define REMOTE_TRAVEL_SPEED_MAX_MPS         1.8f
#define REMOTE_TRAVEL_REVERSE_FACTOR        0.8f
#define REMOTE_IBUS_TIMEOUT_MS            100U
#define REMOTE_IBUS_STABLE_MIN_FRAMES     20U
#define REMOTE_IBUS_STABLE_MIN_MS         150U
#define REMOTE_LEGACY_TIMEOUT_MS          300U
#define REMOTE_SWITCH_CONFIRM_FRAMES       3U
#define REMOTE_UNLOCK_WINDOW_MS            3000U
#define REMOTE_STICK_CENTER_TOLERANCE_US   50U
#define REMOTE_STICK_CENTER_HOLD_MS        200U
#define REMOTE_SWITCH_POSITION_TOLERANCE_US 120U

#define REMOTE_IBUS_CHANNEL_STEERING      1U
#define REMOTE_IBUS_CHANNEL_THROTTLE      2U
#define REMOTE_IBUS_CHANNEL_BLADE_START   4U
#define REMOTE_IBUS_CHANNEL_RUN_ENABLE    5U
#define REMOTE_IBUS_CHANNEL_BLADE_CONFIRM 6U
#define REMOTE_IBUS_CHANNEL_BLADE_GEAR    7U
#define REMOTE_IBUS_CHANNEL_MODE          8U
#define REMOTE_IBUS_CHANNEL_UNLOCK        8U /* Legacy / bench profile only. */
#define REMOTE_IBUS_CHANNEL_BLADE_HEIGHT  9U
#define REMOTE_IBUS_CHANNEL_TRAVEL_SPEED  10U

#define REMOTE_BLADE_START_CENTER_US       1500U
#define REMOTE_BLADE_START_START_US        1000U
#define REMOTE_BLADE_START_TOLERANCE_US    100U

#define REMOTE_BLADE_CONFIRM_LOW_US        1000U
#define REMOTE_BLADE_CONFIRM_HIGH_US       2000U
#define REMOTE_BLADE_CONFIRM_TOLERANCE_US  120U
#define REMOTE_BLADE_CONFIRM_FRAMES        3U

#define REMOTE_TRAVEL_SPEED_FAST_US        2000U
#define REMOTE_TRAVEL_SPEED_SLOW_US        1000U

#define REMOTE_BLADE_HEIGHT_T1_US          1200U
#define REMOTE_BLADE_HEIGHT_T2_US          1400U
#define REMOTE_BLADE_HEIGHT_T3_US          1600U
#define REMOTE_BLADE_HEIGHT_T4_US          1800U
#define REMOTE_BLADE_HEIGHT_HYSTERESIS_US  25U
#define REMOTE_IBUS_CHANNEL_INDEX(channel_number) ((uint8_t)((channel_number) - 1U))

typedef struct
{
    uint8_t channel_index;
    uint16_t min_us;
    uint16_t center_us;
    uint16_t max_us;
    bool reversed;
} FlySkyIbusAxisConfig_t;

typedef struct
{
    FlySkyIbusAxisConfig_t steering;
    FlySkyIbusAxisConfig_t throttle;
    uint8_t blade_start_index;
    uint8_t run_enable_index;
    uint8_t blade_confirm_index;
    uint8_t grass_bag_index;
    uint8_t blade_gear_index;
    uint8_t unlock_index;
    uint8_t blade_height_index;
    uint8_t travel_speed_index;
    uint16_t valid_min_us;
    uint16_t valid_max_us;
    uint16_t timeout_ms;
    uint16_t stable_min_ms;
    uint8_t stable_min_frames;
    uint16_t run_enable_off_us;
    uint16_t run_enable_on_us;
    uint16_t unlock_up_us;
    uint16_t unlock_middle_us;
    uint16_t unlock_down_us;
    uint16_t blade_gear_low_us;
    uint16_t blade_gear_medium_us;
    uint16_t blade_gear_high_us;
    uint16_t blade_start_center_us;
    uint16_t blade_start_start_us;
    uint16_t blade_start_tolerance_us;
    uint16_t blade_confirm_low_us;
    uint16_t blade_confirm_high_us;
    uint16_t blade_confirm_tolerance_us;
    uint8_t blade_confirm_frames;
    uint16_t blade_height_t1_us;
    uint16_t blade_height_t2_us;
    uint16_t blade_height_t3_us;
    uint16_t blade_height_t4_us;
    uint16_t blade_height_hysteresis_us;
    uint16_t switch_position_tolerance_us;
    uint16_t stick_center_tolerance_us;
    uint16_t stick_center_hold_ms;
    uint16_t unlock_window_ms;
    uint8_t switch_confirm_frames;
    bool allow_unlock_direct_jump;
    bool run_enable_semantics_configured;
    bool blade_start_semantics_configured;
    bool blade_confirm_semantics_configured;
    bool grass_bag_semantics_configured;
    bool blade_height_semantics_configured;
    bool travel_speed_semantics_configured;
} FlySkyIbusConfig_t;

#endif
