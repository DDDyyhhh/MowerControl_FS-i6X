#ifndef TELEMETRY_SNAPSHOT_H
#define TELEMETRY_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#define TELEMETRY_BMS_FRESHNESS_MS       3000U
#define TELEMETRY_WHEEL_FRESHNESS_MS      150U
#define TELEMETRY_BLADE_POLE_PAIRS          4U

#define TELEMETRY_FAULT_EMERGENCY_ACTIVE     (1U << 0)
#define TELEMETRY_FAULT_COLLISION             (1U << 1)
#define TELEMETRY_FAULT_TILT                  (1U << 2)
#define TELEMETRY_FAULT_IBUS_TIMEOUT          (1U << 3)
#define TELEMETRY_FAULT_REMOTE_STOP_LATCHED   (1U << 4)
#define TELEMETRY_FAULT_OWNERSHIP_TRANSITION  (1U << 5)
#define TELEMETRY_FAULT_BMS_INVALID           (1U << 6)
#define TELEMETRY_FAULT_WHEEL_STALE           (1U << 7)
#define TELEMETRY_FAULT_BLADE_STALE           (1U << 8)
#define TELEMETRY_FAULT_FTESC_CRC             (1U << 9)
#define TELEMETRY_FAULT_DEFINED_MASK          0x03FFU

typedef enum
{
    TELEMETRY_VALUE_BATTERY_VOLTAGE = 0,
    TELEMETRY_VALUE_BATTERY_CURRENT,
    TELEMETRY_VALUE_SOC,
    TELEMETRY_VALUE_GROUND_SPEED,
    TELEMETRY_VALUE_BLADE_RPM,
    TELEMETRY_VALUE_WORK_MODE,
    TELEMETRY_VALUE_RUN_PERMISSION,
    TELEMETRY_VALUE_DETAILED_FAULT,
    TELEMETRY_VALUE_BAG_CONNECTION,
    TELEMETRY_VALUE_COUNT
} TelemetryValueIndex_t;

typedef enum
{
    TELEMETRY_WORK_MODE_UNAVAILABLE = 0,
    TELEMETRY_WORK_MODE_MANUAL_REMOTE = 1
} TelemetryWorkMode_t;

typedef struct
{
    bool valid;
    uint32_t last_good_ms;
    uint32_t voltage_centivolts;
    int32_t current_deciamps;
    uint16_t soc_percent;
} TelemetryBmsSource_t;

typedef struct
{
    bool valid;
    uint32_t last_good_ms;
    int32_t speed_mm_per_s;
} TelemetryWheelSource_t;

typedef struct
{
    bool feedback_enabled;
    bool valid;
    bool crc_error;
    uint32_t last_good_ms;
    uint32_t freshness_ms;
    int32_t actual_erpm;
} TelemetryBladeSource_t;

typedef struct
{
    TelemetryBmsSource_t bms;
    TelemetryWheelSource_t wheels[4];
    TelemetryBladeSource_t blade;
    bool manual_remote_available;
    bool final_manual_run_permitted;
    bool emergency_active;
    bool collision_active_or_latched;
    bool tilt_active_or_latched;
    bool ibus_timeout_latched;
    bool remote_stop_latched;
    bool ownership_transition_latched;
    bool bag_connection_valid;
    bool bag_connected;
} TelemetrySources_t;

typedef struct
{
    uint16_t values[TELEMETRY_VALUE_COUNT];
    uint32_t generated_at_ms;
} TelemetrySnapshot_t;

void TelemetrySnapshot_Reset(void);
void TelemetrySnapshot_Update(const TelemetrySources_t *sources, uint32_t now_ms);
bool TelemetrySnapshot_Read(TelemetrySnapshot_t *snapshot);

#endif
