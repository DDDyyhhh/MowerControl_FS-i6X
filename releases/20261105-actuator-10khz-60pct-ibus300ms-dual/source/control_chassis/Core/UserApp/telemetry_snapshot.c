#include "telemetry_snapshot.h"
#include "../../../experiments/ticket03/battery_status.h"
#include "../../../experiments/ticket03/bag_connection.h"

#include <limits.h>
#include "../../../experiments/ticket03/mower_fast_status.h"
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
    volatile uint32_t fast_status;
    uint32_t heartbeat_tick;
    uint8_t heartbeat, heartbeat_started;
    volatile uint32_t diag_sequence;
    TelemetryWheelDiagnostics_t wheels;
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
    if (sources->system_locked)
        faults |= TELEMETRY_FAULT_SYSTEM_LOCKED;
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
    /* Disabled/unvalidated actual-RPM telemetry is unavailable, not expired. */
    if (sources->blade.feedback_enabled && !blade_fresh)
        faults |= TELEMETRY_FAULT_BLADE_STALE;
    if (sources->blade.crc_error)
        faults |= TELEMETRY_FAULT_FTESC_CRC;

    faults |= sources->battery_status & MOWER_BATTERY_STATUS_MASK;
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

uint16_t TelemetrySnapshot_ReadFastStatus(void)
{
    /* Read the aligned publication once; legacy callers use its low half. */
    return (uint16_t)g_publication.fast_status;
}

uint32_t TelemetrySnapshot_ReadFastAuthorization(void)
{
    /* Aligned Cortex-M4 word: one ISR read, never a writer-dependent loop. */
    return g_publication.fast_status;
}

bool TelemetrySnapshot_ReadWheelDiagnostics(TelemetryWheelDiagnostics_t *out)
{
    if (!out) return false;
    for (unsigned retry=0; retry<3U; ++retry) {
        uint32_t before = g_publication.diag_sequence;
        if (before & 1U) return false;
        TELEMETRY_COMPILER_BARRIER();
        *out = g_publication.wheels;
        TELEMETRY_COMPILER_BARRIER();
        if (before == g_publication.diag_sequence) return true;
    }
    return false;
}

static void TelemetrySnapshot_RecordWheels(const TelemetrySources_t *s, uint32_t now)
{
    TelemetryWheelDiagnostics_t *d = &g_publication.wheels;
    uint8_t stale = 0U;
    for (unsigned i=0; i<4U; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        d->age_ms[i] = s->wheels[i].valid ? now - s->wheels[i].last_good_ms : UINT32_MAX;
        if (!s->wheels[i].valid || d->age_ms[i] > TELEMETRY_WHEEL_FRESHNESS_MS) stale |= bit;
        if (s->wheels[i].valid) {
            if ((d->valid_mask & bit) && d->last_good_ms[i] != s->wheels[i].last_good_ms) {
                uint32_t gap = s->wheels[i].last_good_ms - d->last_good_ms[i];
                if (gap > d->max_gap_ms[i]) d->max_gap_ms[i] = gap;
            }
            d->last_good_ms[i] = s->wheels[i].last_good_ms;
            d->valid_mask |= bit;
        }
    }
    d->transport = s->transport;
    if (stale && !d->stale_mask) {
        ++d->fault_events; d->fault_at_ms = now; d->fault_stale_mask = stale;
        memcpy(d->fault_age_ms, d->age_ms, sizeof(d->age_ms));
        d->fault_transport = s->transport;
    }
    d->stale_mask = stale;
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
        snapshot.values[TELEMETRY_VALUE_BATTERY_MIN_CELL] = sources->bms.min_cell_mv;
    }

    snapshot.values[TELEMETRY_VALUE_BATTERY_REASON] = sources->battery_reasons;

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
    if (sources->bag_connection_valid && sources->bag_connected && sources->bag_full_valid)
        snapshot.values[TELEMETRY_VALUE_BAG_FULL] = (uint16_t)(MOWER_BAG_FULL_VALID |
            (sources->bag_full ? MOWER_BAG_FULL : 0U));

    /* Preserve the producer heartbeat even when Motion keeps publishing. */
    snapshot.values[TELEMETRY_VALUE_HEIGHT_FEEDBACK] = sources->height_feedback;
    /* Advance by elapsed intervals, so HAL's 32-bit wrap does not jump the
     * 7-bit wire heartbeat backwards or keep a frozen producer alive. */
    if (!g_publication.heartbeat_started) {
        g_publication.heartbeat_started = 1U;
        g_publication.heartbeat_tick = now_ms;
    } else {
        uint32_t steps = (uint32_t)(now_ms-g_publication.heartbeat_tick) / MOWER_FAST_HEARTBEAT_MS;
        g_publication.heartbeat = (uint8_t)(g_publication.heartbeat + steps);
        g_publication.heartbeat_tick += steps * MOWER_FAST_HEARTBEAT_MS;
    }
    snapshot.values[TELEMETRY_VALUE_FAST_STATUS] = mower_fast_status_pack(
        sources->blade_output_bits, snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT],
        (uint8_t)snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION],
        (uint32_t)g_publication.heartbeat * MOWER_FAST_HEARTBEAT_MS);
    /* The diagnostic copy is task-only; publish its sequence around the copy. */
    g_publication.diag_sequence++;
    TELEMETRY_COMPILER_BARRIER();
    TelemetrySnapshot_RecordWheels(sources, now_ms);
    TELEMETRY_COMPILER_BARRIER();
    g_publication.diag_sequence++;
    g_publication.fast_status = snapshot.values[TELEMETRY_VALUE_FAST_STATUS] |
        ((uint32_t)sources->authorization << 16);
    TelemetrySnapshot_Publish(&snapshot);
}

bool TelemetrySnapshot_Read(TelemetrySnapshot_t *snapshot)
{
    if (!snapshot || !g_publication.available) return false;
    /* USART2 can preempt Motion. An odd sequence must return unavailable,
     * never spin waiting for a writer that cannot resume until this ISR exits. */
    for (unsigned retry=0; retry<3U; ++retry) {
        uint32_t before = g_publication.sequence;
        if (before & 1U) return false;
        uint8_t active = g_publication.active_slot;
        TELEMETRY_COMPILER_BARRIER();
        *snapshot = g_publication.slots[active];
        TELEMETRY_COMPILER_BARRIER();
        if (before == g_publication.sequence) return true;
    }
    return false;
}
