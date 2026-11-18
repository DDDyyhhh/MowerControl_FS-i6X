#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#define BATTERY_FRESHNESS_MS 3000U
#define BATTERY_MAX_SAMPLE_GAP_MS 1500U

typedef enum {
    BATTERY_INVALID = 0, BATTERY_NORMAL, BATTERY_LOW, BATTERY_STOP
} BatteryState_t;

#define BATTERY_REASON_DATA 0x01U
#define BATTERY_REASON_PACK 0x04U
#define BATTERY_REASON_SOC  0x20U

typedef struct {
    uint32_t sample_ms, pack_cv;
    int32_t current_da;
    uint32_t fcc_cah, rc_cah;
    uint16_t min_cell_mv, max_cell_mv, cell_numbers, soc_percent;
    uint16_t max_temp_dc, min_temp_dc;
    bool valid;
} BatterySample_t;

/* Motion is the only writer. Keep only the latest sample; no history or IO. */
typedef struct {
    BatterySample_t latest;
    BatteryState_t state;
    uint16_t reasons, low_reasons;
    bool has_sample, data_fresh, startup_checked;
    bool low_latched, stop_latched, recovery_ready;
    bool stop_required, blade_inhibited;
    uint32_t low_since, recovery_since;
    bool low_pending, recovery_pending;
} BatteryMonitor_t;

bool BatterySample_IsValid(const BatterySample_t *sample);
void BatteryMonitor_Init(BatteryMonitor_t *monitor);
void BatteryMonitor_Update(BatteryMonitor_t *monitor, const BatterySample_t *sample, uint32_t now_ms);
/* Compatibility hook; DATA is diagnostic only. LOW clears only at Init/reboot. */
bool BatteryMonitor_Rearm(BatteryMonitor_t *monitor);

#endif
