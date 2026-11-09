#include "telemetry_snapshot.h"
#include "../../../experiments/ticket03/bag_connection.h"

#include <limits.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define TELEMETRY_COMPILER_BARRIER() __asm volatile ("" ::: "memory")
#else
#define TELEMETRY_COMPILER_BARRIER() ((void)0)
#endif

typedef struct
{
    TelemetrySnapshot_t slots[2];
    volatile uint8_t active_slot;
    volatile uint8_t available;
    volatile uint32_t sequence;
} TelemetryPublication_t;

static TelemetryPublication_t g_publication;

static uint16_t TelemetrySnapshot_SaturateU16(uint64_t value)
{
    return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
}

static uint64_t TelemetrySnapshot_AbsI32(int32_t value)
{
    if (value >= 0)
    {
        return (uint64_t)(uint32_t)value;
    }
    return (uint64_t)(-(int64_t)value);
}

static bool TelemetrySnapshot_BmsFresh(const TelemetryBmsSource_t *bms,
                                       uint32_t now_ms)
{
    return bms->valid &&
           (bms->voltage_centivolts > 0U) &&
           (bms->soc_percent <= 100U) &&
           ((uint32_t)(now_ms - bms->last_good_ms) <
            TELEMETRY_BMS_FRESHNESS_MS);
}

static bool TelemetrySnapshot_WheelsFresh(const TelemetryWheelSource_t wheels[4],
                                          uint32_t now_ms)
{
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        if (!wheels[i].valid ||
            ((uint32_t)(now_ms - wheels[i].last_good_ms) >
             TELEMETRY_WHEEL_FRESHNESS_MS))
        {
            return false;
        }
    }
    return true;
}

static uint16_t TelemetrySnapshot_GroundSpeed(
    const TelemetryWheelSource_t wheels[4])
{
    int64_t sum = (int64_t)wheels[0].speed_mm_per_s +
                  (int64_t)wheels[1].speed_mm_per_s +
                  (int64_t)wheels[2].speed_mm_per_s +
                  (int64_t)wheels[3].speed_mm_per_s;
    uint64_t magnitude = (sum < 0) ? (uint64_t)(-sum) : (uint64_t)sum;

    return TelemetrySnapshot_SaturateU16((magnitude + 20U) / 40U);
}

static bool TelemetrySnapshot_BladeFresh(const TelemetryBladeSource_t *blade,
                                         uint32_t now_ms)
{
    return blade->feedback_enabled &&
           blade->valid &&
           (blade->freshness_ms > 0U) &&
           ((uint32_t)(now_ms - blade->last_good_ms) <= blade->freshness_ms);
}

static uint16_t TelemetrySnapshot_BuildFaults(
    const TelemetrySources_t *sources,
    bool bms_fresh,
    bool wheels_fresh,
    bool blade_fresh)
{
    uint16_t faults = 0U;

    if (sources->emergency_active)
        faults |= TELEMETRY_FAULT_EMERGENCY_ACTIVE;
    if (sources->collision_active_or_latched)
        faults |= TELEMETRY_FAULT_COLLISION;
    if (sources->tilt_active_or_latched)
        faults |= TELEMETRY_FAULT_TILT;
    if (sources->ibus_timeout_latched)
        faults |= TELEMETRY_FAULT_IBUS_TIMEOUT;
    if (sources->remote_stop_latched)
        faults |= TELEMETRY_FAULT_REMOTE_STOP_LATCHED;
    if (sources->ownership_transition_latched)
        faults |= TELEMETRY_FAULT_OWNERSHIP_TRANSITION;
    if (!bms_fresh)
        faults |= TELEMETRY_FAULT_BMS_INVALID;
    if (!wheels_fresh)
        faults |= TELEMETRY_FAULT_WHEEL_STALE;
    if (!blade_fresh)
        faults |= TELEMETRY_FAULT_BLADE_STALE;
    if (sources->blade.crc_error)
        faults |= TELEMETRY_FAULT_FTESC_CRC;

    return faults & TELEMETRY_FAULT_DEFINED_MASK;
}

static void TelemetrySnapshot_Publish(const TelemetrySnapshot_t *snapshot)
{
    uint8_t next = (uint8_t)(g_publication.active_slot ^ 1U);

    g_publication.slots[next] = *snapshot;
    TELEMETRY_COMPILER_BARRIER();
    g_publication.sequence++;
    g_publication.active_slot = next;
    g_publication.available = 1U;
    TELEMETRY_COMPILER_BARRIER();
    g_publication.sequence++;
}

void TelemetrySnapshot_Reset(void)
{
    memset(&g_publication, 0, sizeof(g_publication));
}

void TelemetrySnapshot_Update(const TelemetrySources_t *sources, uint32_t now_ms)
{
    TelemetrySnapshot_t snapshot;
    bool bms_fresh;
    bool wheels_fresh;
    bool blade_fresh;

    if (sources == NULL)
    {
        return;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    bms_fresh = TelemetrySnapshot_BmsFresh(&sources->bms, now_ms);
    wheels_fresh = TelemetrySnapshot_WheelsFresh(sources->wheels, now_ms);
    blade_fresh = TelemetrySnapshot_BladeFresh(&sources->blade, now_ms);

    if (bms_fresh)
    {
        snapshot.values[TELEMETRY_VALUE_BATTERY_VOLTAGE] =
            TelemetrySnapshot_SaturateU16(sources->bms.voltage_centivolts);
        snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] =
            TelemetrySnapshot_SaturateU16(
                TelemetrySnapshot_AbsI32(sources->bms.current_deciamps) * 10U);
        snapshot.values[TELEMETRY_VALUE_SOC] = sources->bms.soc_percent;
    }

    if (wheels_fresh)
    {
        snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] =
            TelemetrySnapshot_GroundSpeed(sources->wheels);
    }

    if (blade_fresh)
    {
        snapshot.values[TELEMETRY_VALUE_BLADE_RPM] =
            TelemetrySnapshot_SaturateU16(
                TelemetrySnapshot_AbsI32(sources->blade.actual_erpm) /
                TELEMETRY_BLADE_POLE_PAIRS);
    }

    snapshot.values[TELEMETRY_VALUE_WORK_MODE] =
        sources->manual_remote_available ? TELEMETRY_WORK_MODE_MANUAL_REMOTE
                                         : TELEMETRY_WORK_MODE_UNAVAILABLE;
    snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] =
        (sources->manual_remote_available &&
         sources->final_manual_run_permitted) ? 1U : 0U;
    snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] =
        TelemetrySnapshot_BuildFaults(sources, bms_fresh, wheels_fresh,
                                      blade_fresh);
    snapshot.generated_at_ms = now_ms;
    snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] = (uint16_t)(
        (((now_ms / MOWER_BAG_HEARTBEAT_MS) & MOWER_BAG_HEARTBEAT_MASK) << 2) |
        (sources->bag_connection_valid ? MOWER_BAG_SAMPLE_VALID : 0U) |
        (sources->bag_connection_valid && sources->bag_connected ? MOWER_BAG_CONNECTED : 0U));

    TelemetrySnapshot_Publish(&snapshot);
}

bool TelemetrySnapshot_Read(TelemetrySnapshot_t *snapshot)
{
    uint32_t sequence_before = 0U;
    uint32_t sequence_after = 0U;
    uint8_t active;

    if ((snapshot == NULL) || (g_publication.available == 0U))
    {
        return false;
    }

    do
    {
        sequence_before = g_publication.sequence;
        if ((sequence_before & 1U) != 0U)
        {
            continue;
        }
        active = g_publication.active_slot;
        TELEMETRY_COMPILER_BARRIER();
        *snapshot = g_publication.slots[active];
        TELEMETRY_COMPILER_BARRIER();
        sequence_after = g_publication.sequence;
    } while ((sequence_before != sequence_after) ||
             ((sequence_after & 1U) != 0U));

    return true;
}
