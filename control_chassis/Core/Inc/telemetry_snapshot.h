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
#define TELEMETRY_FAULT_SYSTEM_LOCKED         (1U << 10)
#define TELEMETRY_FAULT_DEFINED_MASK          0x3FFFU

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
    TELEMETRY_VALUE_HEIGHT_FEEDBACK,
    TELEMETRY_VALUE_FAST_STATUS,
    TELEMETRY_VALUE_BATTERY_MIN_CELL,
    TELEMETRY_VALUE_BATTERY_REASON,
    TELEMETRY_VALUE_BAG_FULL,
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
    uint16_t min_cell_mv;
} TelemetryBmsSource_t;

typedef struct
{
    bool valid;
    uint32_t last_good_ms;
    int32_t speed_mm_per_s;
} TelemetryWheelSource_t;

typedef struct
{
    bool feedback_enabled; /* Hardware-validated RPM; false suppresses only stale. */
    bool valid;
    bool crc_error;
    uint32_t last_good_ms;
    uint32_t freshness_ms;
    int32_t actual_erpm;
} TelemetryBladeSource_t;

typedef struct {
    uint32_t query_failures, mailbox_failures, rx_overflows, bus_error_code;
} TelemetryTransportDiagnostics_t;
typedef struct {
    uint32_t age_ms[4], max_gap_ms[4];
    uint32_t last_good_ms[4];
    uint8_t valid_mask, stale_mask;
    uint32_t fault_events, fault_at_ms;
    uint32_t fault_age_ms[4];
    uint8_t fault_stale_mask;
    TelemetryTransportDiagnostics_t transport, fault_transport;
} TelemetryWheelDiagnostics_t;

typedef struct
{
    TelemetryBmsSource_t bms;
    uint16_t battery_status, battery_reasons;
    TelemetryWheelSource_t wheels[4];
    TelemetryBladeSource_t blade;
    bool manual_remote_available;
    bool final_manual_run_permitted;
    bool emergency_active; /* Display cause: active input or latched emergency. */
    bool system_locked;
    bool collision_active_or_latched;
    bool tilt_active_or_latched;
    bool ibus_timeout_latched; /* Current timeout stop, not lifetime RX history. */
    bool remote_stop_latched;
    bool ownership_transition_latched;
    bool bag_connection_valid;
    bool bag_connected;
    bool bag_full_valid;
    bool bag_full;
    uint16_t height_feedback;
    uint8_t blade_output_bits;
    uint16_t authorization; /* Motion-only read-only E9 profile 3 authority. */
    TelemetryTransportDiagnostics_t transport;
} TelemetrySources_t;

typedef struct
{
    uint16_t values[TELEMETRY_VALUE_COUNT];
    uint32_t generated_at_ms;
} TelemetrySnapshot_t;

uint16_t TelemetrySnapshot_ReadFastStatus(void);
uint32_t TelemetrySnapshot_ReadFastAuthorization(void);
bool TelemetrySnapshot_ReadWheelDiagnostics(TelemetryWheelDiagnostics_t *out);
void TelemetrySnapshot_Reset(void);
void TelemetrySnapshot_Update(const TelemetrySources_t *sources, uint32_t now_ms);
bool TelemetrySnapshot_Read(TelemetrySnapshot_t *snapshot);

#endif
