#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "telemetry_snapshot.h"
#include "../../experiments/ticket03/bag_connection.h"
#include "../../experiments/ticket03/battery_status.h"
#include "../../experiments/ticket03/mower_fast_status.h"

static void TestHeightProducerHeartbeat(void)
{
    TelemetrySources_t sources = {0};
    TelemetrySnapshot_t snapshot;
    sources.height_feedback = 0x0AC6U; // hb10, valid, 7.0cm
    TelemetrySnapshot_Update(&sources, 1000U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_HEIGHT_FEEDBACK] == 0x0AC6U);
    TelemetrySnapshot_Update(&sources, 5000U); // Motion cannot renew a frozen lifter.
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_HEIGHT_FEEDBACK] == 0x0AC6U);
    assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
    sources.height_feedback = 0U;
    TelemetrySnapshot_Update(&sources, 5100U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_HEIGHT_FEEDBACK] == 0U);
}

static void TestBagConnection(void)
{
    TelemetrySources_t sources = {0};
    TelemetrySnapshot_t snapshot;
    TelemetrySnapshot_Update(&sources, 1000U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] == 40U);
    sources.bag_connection_valid = true;
    TelemetrySnapshot_Update(&sources, 1099U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] == 42U);
    sources.bag_connected = true;
    TelemetrySnapshot_Update(&sources, 1100U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] == 47U);
    assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
    TelemetrySnapshot_Update(&sources, 1638400U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] == 3U);
    sources.bag_connection_valid = false;
    TelemetrySnapshot_Update(&sources, 1638500U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] == 4U);
    for (unsigned mask=0U; mask<16U; ++mask) {
        sources.bag_connection_valid = (mask & 1U) != 0U;
        sources.bag_connected = (mask & 2U) != 0U;
        sources.bag_full_valid = (mask & 4U) != 0U;
        sources.bag_full = (mask & 8U) != 0U;
        TelemetrySnapshot_Update(&sources, 2000U);
        assert(TelemetrySnapshot_Read(&snapshot));
        uint16_t expected=(mask & 7U)==7U ?
            (uint16_t)(MOWER_BAG_FULL_VALID | ((mask & 8U) ? MOWER_BAG_FULL : 0U)) : 0U;
        assert(snapshot.values[TELEMETRY_VALUE_BAG_FULL] == expected);
        assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
    }
}

static TelemetrySources_t FreshSources(uint32_t now_ms)
{
    TelemetrySources_t sources;
    uint8_t i;

    memset(&sources, 0, sizeof(sources));
    sources.bms.valid = true;
    sources.bms.last_good_ms = now_ms;
    sources.bms.voltage_centivolts = 4850U;
    sources.bms.current_deciamps = -123;
    sources.bms.soc_percent = 80U;
    for (i = 0U; i < 4U; i++)
    {
        sources.wheels[i].valid = true;
        sources.wheels[i].last_good_ms = now_ms;
        sources.wheels[i].speed_mm_per_s = 1000;
    }
    sources.manual_remote_available = true;
    sources.final_manual_run_permitted = true;
    return sources;
}

static TelemetrySnapshot_t PublishAndRead(const TelemetrySources_t *sources,
                                          uint32_t now_ms)
{
    TelemetrySnapshot_t snapshot;

    TelemetrySnapshot_Update(sources, now_ms);
    assert(TelemetrySnapshot_Read(&snapshot));
    return snapshot;
}

static void TestScaledValuesAndModes(void)
{
    TelemetrySources_t sources = FreshSources(1000U);
    TelemetrySnapshot_t snapshot = PublishAndRead(&sources, 1000U);

    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_VOLTAGE] == 4850U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] == 1230U);
    assert(snapshot.values[TELEMETRY_VALUE_SOC] == 80U);
    assert(snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] == 100U);
    assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 0U);
    assert(snapshot.values[TELEMETRY_VALUE_WORK_MODE] == TELEMETRY_WORK_MODE_MANUAL_REMOTE);
    assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 1U);
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] == 0U);
}

static void TestBmsBoundariesAndFreshness(void)
{
    TelemetrySources_t sources = FreshSources(10U);
    TelemetrySnapshot_t snapshot;

    sources.bms.soc_percent = 0U;
    snapshot = PublishAndRead(&sources, 3009U);
    assert(snapshot.values[TELEMETRY_VALUE_SOC] == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BMS_INVALID) == 0U);

    sources.bms.soc_percent = 100U;
    snapshot = PublishAndRead(&sources, 3009U);
    assert(snapshot.values[TELEMETRY_VALUE_SOC] == 100U);

    snapshot = PublishAndRead(&sources, 3010U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_VOLTAGE] == 0U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] == 0U);
    assert(snapshot.values[TELEMETRY_VALUE_SOC] == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BMS_INVALID) != 0U);

    sources = FreshSources(100U);
    sources.bms.soc_percent = 101U;
    snapshot = PublishAndRead(&sources, 100U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BMS_INVALID) != 0U);

    sources = FreshSources(UINT32_MAX - 1000U);
    snapshot = PublishAndRead(&sources, 500U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_VOLTAGE] == 4850U);
}

static void TestCurrentMagnitudeAndSaturation(void)
{
    TelemetrySources_t sources = FreshSources(0U);
    TelemetrySnapshot_t snapshot;

    sources.bms.current_deciamps = 123;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] == 1230U);

    sources.bms.current_deciamps = -7000;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] == UINT16_MAX);

    sources.bms.current_deciamps = INT32_MIN;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_BATTERY_CURRENT] == UINT16_MAX);
}

static void TestWheelAverageFreshnessAndSaturation(void)
{
    TelemetrySources_t sources = FreshSources(1000U);
    TelemetrySnapshot_t snapshot;

    sources.wheels[0].speed_mm_per_s = 1000;
    sources.wheels[1].speed_mm_per_s = 500;
    sources.wheels[2].speed_mm_per_s = -500;
    sources.wheels[3].speed_mm_per_s = 1000;
    snapshot = PublishAndRead(&sources, 1150U);
    assert(snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] == 50U);

    sources.wheels[3].last_good_ms = 999U;
    snapshot = PublishAndRead(&sources, 1150U);
    assert(snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_WHEEL_STALE) != 0U);

    sources = FreshSources(0U);
    sources.wheels[0].valid = false;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] == 0U);

    sources = FreshSources(0U);
    sources.wheels[0].speed_mm_per_s = INT32_MAX;
    sources.wheels[1].speed_mm_per_s = INT32_MAX;
    sources.wheels[2].speed_mm_per_s = INT32_MAX;
    sources.wheels[3].speed_mm_per_s = INT32_MAX;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_GROUND_SPEED] == UINT16_MAX);
}

static void TestBladeFeedbackPolicy(void)
{
    TelemetrySources_t sources = FreshSources(100U);
    TelemetrySnapshot_t snapshot;

    sources.blade.feedback_enabled = true;
    sources.blade.valid = true;
    sources.blade.last_good_ms = 100U;
    sources.blade.freshness_ms = 500U;
    sources.blade.actual_erpm = -16000;
    snapshot = PublishAndRead(&sources, 600U);
    assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 4000U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) == 0U);

    snapshot = PublishAndRead(&sources, 601U);
    assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) != 0U);

    sources.blade.crc_error = true;
    snapshot = PublishAndRead(&sources, 601U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_FTESC_CRC) != 0U);

    sources.blade.feedback_enabled = false;
    sources.blade.valid = true;
    sources.blade.last_good_ms = 601U;
    snapshot = PublishAndRead(&sources, 601U);
    assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_FTESC_CRC) != 0U);
}

static void TestBladeDisabledAndWrap(void)
{
    uint32_t start = UINT32_MAX - 100U;
    TelemetrySources_t sources = FreshSources(start);
    TelemetrySnapshot_t snapshot;
    /* Not enabled, no feedback: no alarm and no invented actual RPM. */
    snapshot = PublishAndRead(&sources, start);
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] == 0U);
    assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 0U);
    sources.blade.feedback_enabled = true;
    snapshot = PublishAndRead(&sources, start);
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] == TELEMETRY_FAULT_BLADE_STALE);
    sources.blade.valid = true;
    sources.blade.last_good_ms = start;
    sources.blade.freshness_ms = 500U;
    snapshot = PublishAndRead(&sources, 399U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) == 0U);
    snapshot = PublishAndRead(&sources, 400U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) != 0U);
    sources.blade.feedback_enabled = false;
    snapshot = PublishAndRead(&sources, 400U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) == 0U);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_WHEEL_STALE) != 0U);
    sources.blade.feedback_enabled = true;
    sources.blade.freshness_ms = 0U;
    snapshot = PublishAndRead(&sources, start);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BLADE_STALE) != 0U);
}

static void TestFaultBitsAndFinalGateSemantics(void)
{
    TelemetrySources_t sources = FreshSources(0U);
    TelemetrySnapshot_t snapshot;
    uint16_t expected;

    sources.final_manual_run_permitted = false;
    sources.emergency_active = true;
    sources.system_locked = true;
    sources.collision_active_or_latched = true;
    sources.tilt_active_or_latched = true;
    sources.ibus_timeout_latched = true;
    sources.remote_stop_latched = true;
    sources.ownership_transition_latched = true;
    sources.bms.valid = false;
    sources.wheels[2].valid = false;
    sources.blade.feedback_enabled = true;
    sources.blade.crc_error = true;

    snapshot = PublishAndRead(&sources, 0U);
    expected = 0x07FFU; /* Existing faults; battery states tested separately. */
    assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] == expected);
    assert((snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] & 0xF800U) == 0U);

    sources.manual_remote_available = false;
    sources.final_manual_run_permitted = true;
    snapshot = PublishAndRead(&sources, 0U);
    assert(snapshot.values[TELEMETRY_VALUE_WORK_MODE] == TELEMETRY_WORK_MODE_UNAVAILABLE);
    assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
}

static void TestBatteryDiagnostics(void)
{
    TelemetrySources_t s = FreshSources(1000U);
    TelemetrySnapshot_t out;
    s.bms.voltage_centivolts = 4700U; s.bms.soc_percent = 29U;
    s.bms.min_cell_mv = 2930U; s.battery_reasons = 4U;
    s.battery_status = MOWER_BATTERY_WARNING | MOWER_BATTERY_BLADE_INHIBIT;
    s.final_manual_run_permitted = true;
    out = PublishAndRead(&s, 1000U);
    assert(out.values[TELEMETRY_VALUE_SOC] == 29U);
    assert(out.values[TELEMETRY_VALUE_BATTERY_MIN_CELL] == 2930U);
    assert(out.values[TELEMETRY_VALUE_BATTERY_REASON] == 4U);
    assert(out.values[TELEMETRY_VALUE_DETAILED_FAULT] == s.battery_status);
    assert(out.values[TELEMETRY_VALUE_RUN_PERMISSION] == 1U);
    assert(TelemetrySnapshot_ReadFastStatus() & MOWER_FAST_FAULT);
    s.battery_status = 0U;
    out = PublishAndRead(&s, 1000U);
    assert(!(TelemetrySnapshot_ReadFastStatus() & MOWER_FAST_FAULT));
    out = PublishAndRead(&s, 4000U);
    assert(out.values[TELEMETRY_VALUE_BATTERY_MIN_CELL] == 0U);
    assert(out.values[TELEMETRY_VALUE_DETAILED_FAULT] & TELEMETRY_FAULT_BMS_INVALID);
}

static void TestCompleteSnapshotPublication(void)
{
    TelemetrySources_t old_sources = FreshSources(10U);
    TelemetrySources_t new_sources = FreshSources(20U);
    TelemetrySnapshot_t old_snapshot;
    TelemetrySnapshot_t new_snapshot;
    TelemetrySnapshot_t readback;

    old_sources.bms.voltage_centivolts = 1111U;
    old_sources.bms.soc_percent = 11U;
    new_sources.bms.voltage_centivolts = 2222U;
    new_sources.bms.soc_percent = 22U;
    new_sources.final_manual_run_permitted = false;

    TelemetrySnapshot_Reset();
    assert(!TelemetrySnapshot_Read(&readback));
    old_snapshot = PublishAndRead(&old_sources, 10U);
    TelemetrySnapshot_Update(&new_sources, 20U);
    assert(TelemetrySnapshot_Read(&new_snapshot));
    assert(new_snapshot.generated_at_ms == 20U);
    assert(new_snapshot.values[TELEMETRY_VALUE_BATTERY_VOLTAGE] == 2222U);
    assert(new_snapshot.values[TELEMETRY_VALUE_SOC] == 22U);
    assert(new_snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 0U);
    assert(memcmp(&old_snapshot, &new_snapshot, sizeof(old_snapshot)) != 0);
}

int main(void)
{
    TestBatteryDiagnostics();
    TestBagConnection();
    TestHeightProducerHeartbeat();
    TelemetrySnapshot_Reset();
    TestScaledValuesAndModes();
    TestBmsBoundariesAndFreshness();
    TestCurrentMagnitudeAndSaturation();
    TestWheelAverageFreshnessAndSaturation();
    TestBladeFeedbackPolicy();
    TestBladeDisabledAndWrap();
    TestFaultBitsAndFinalGateSemantics();
    TestCompleteSnapshotPublication();
    return 0;
}
