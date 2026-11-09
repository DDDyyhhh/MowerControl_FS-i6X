#ifndef FLYSKY_IBUS_SERVO_H
#define FLYSKY_IBUS_SERVO_H

#include <stdbool.h>
#include <stdint.h>

#include "remote_backend_config.h"

typedef struct
{
    uint16_t steering;
    uint16_t throttle;
    uint16_t blade_start_raw;
    uint16_t blade_confirm_raw;
    uint16_t grass_bag_raw;
    bool grass_bag_configured;
    uint16_t blade_height_raw;
    uint16_t travel_speed_raw;
    uint16_t run_enable_raw;
    uint16_t blade_gear_raw;
    uint16_t unlock_raw;
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    bool run_enable_configured;
    bool blade_start_configured;
    bool blade_confirm_configured;
    bool blade_height_configured;
    bool travel_speed_configured;
    bool link_fresh;
    bool stable_online;
    bool timeout_latched;
    uint32_t frame_sequence;
    uint32_t timeout_sequence;
    uint32_t last_valid_ms;
} FlySkyIbusSnapshot_t;

typedef struct
{
    FlySkyIbusConfig_t config;
    FlySkyIbusSnapshot_t snapshots[2];
    uint8_t stream[REMOTE_IBUS_FRAME_SIZE];
    uint8_t stream_count;
    uint8_t active_snapshot;
    uint8_t snapshot_available;
    uint32_t publication_sequence;
    uint8_t consecutive_valid_frames;
    uint8_t timed_out;
    uint32_t stability_started_ms;
} FlySkyIbusServo_t;

FlySkyIbusConfig_t FlySkyIbusServo_DefaultConfig(void);
void FlySkyIbusServo_Init(FlySkyIbusServo_t *ibus, const FlySkyIbusConfig_t *config);
void FlySkyIbusServo_Feed(FlySkyIbusServo_t *ibus,
                          const uint8_t *data,
                          uint16_t length,
                          uint32_t now_ms);
void FlySkyIbusServo_Advance(FlySkyIbusServo_t *ibus, uint32_t now_ms);
void FlySkyIbusServo_ResetStream(FlySkyIbusServo_t *ibus);
bool FlySkyIbusServo_ReadSnapshot(const FlySkyIbusServo_t *ibus,
                                  FlySkyIbusSnapshot_t *snapshot);
bool FlySkyIbusServo_IsStableOnline(const FlySkyIbusServo_t *ibus);
uint32_t FlySkyIbusServo_LastReceiveAgeMs(const FlySkyIbusServo_t *ibus,
                                          uint32_t now_ms);

#endif
