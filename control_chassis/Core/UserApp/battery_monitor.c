#include "battery_monitor.h"
#include "battery_monitor_config.h"

#include <stddef.h>
#include <string.h>

bool BatterySample_IsValid(const BatterySample_t *s)
{
    return s != NULL && s->valid && s->pack_cv > 0U &&
           s->soc_percent <= 100U && s->min_cell_mv > 0U &&
           s->min_cell_mv <= s->max_cell_mv && s->max_cell_mv <= 5000U &&
           s->max_temp_dc <= 1000U;
}

static void RefreshOutputs(BatteryMonitor_t *m)
{
    /* Feedback validity is diagnostic only. A confirmed LOW remains latched,
     * and a valid first LOW sample still closes the startup debounce window. */
    m->stop_required = m->stop_latched = false;
    m->blade_inhibited = m->low_latched || (m->data_fresh && !m->startup_checked);
    m->state = !m->data_fresh ? BATTERY_INVALID :
               m->low_latched ? BATTERY_LOW : BATTERY_NORMAL;
    m->reasons = m->low_reasons | (!m->data_fresh ? BATTERY_REASON_DATA : 0U);
}

void BatteryMonitor_Init(BatteryMonitor_t *m)
{
    memset(m, 0, sizeof(*m));
    RefreshOutputs(m);
}

static bool Confirm(bool condition, bool *pending, uint32_t *since, uint32_t tick, uint32_t delay)
{
    if (!condition) { *pending = false; return false; }
    if (!*pending) { *pending = true; *since = tick; }
    return (uint32_t)(tick - *since) >= delay;
}

void BatteryMonitor_Update(BatteryMonitor_t *m, const BatterySample_t *s, uint32_t now)
{
    bool new_sample = s != NULL && (!m->has_sample || s->sample_ms != m->latest.sample_ms);
    if (new_sample) {
        if (!m->has_sample || (uint32_t)(s->sample_ms - m->latest.sample_ms) > BATTERY_MAX_SAMPLE_GAP_MS) {
            m->low_pending = m->recovery_pending = false;
        }
        m->latest = *s; m->has_sample = true;
    }
    m->data_fresh = m->has_sample && BatterySample_IsValid(&m->latest) &&
                    (uint32_t)(now - m->latest.sample_ms) < BATTERY_FRESHNESS_MS;
    if (!m->data_fresh) {
        m->low_pending = m->recovery_pending = m->recovery_ready = false;
        /* Do not manufacture a healthy sample or clear an established LOW. */
    } else if (new_sample) {
        uint16_t cause = (s->soc_percent < BATTERY_LOW_SOC_PERCENT ? BATTERY_REASON_SOC : 0U) |
                         (s->pack_cv < BATTERY_LOW_PACK_CV ? BATTERY_REASON_PACK : 0U);
        if (Confirm(cause != 0U, &m->low_pending, &m->low_since,
                    s->sample_ms, BATTERY_LOW_CONFIRM_MS)) {
            m->low_latched = true;
            m->low_reasons |= cause;
        }
        /* A low battery at boot cannot briefly start blades during debounce. */
        if (cause == 0U || m->low_latched) m->startup_checked = true;
        m->recovery_ready = Confirm(true, &m->recovery_pending, &m->recovery_since,
                                    s->sample_ms, BATTERY_DATA_RECOVERY_MS);
    }
    RefreshOutputs(m);
}

bool BatteryMonitor_Rearm(BatteryMonitor_t *m)
{
    /* Compatibility hook: feedback faults no longer need rearming.
     * Do not clear low_latched: unlocking travel cannot authorize a blade. */
    RefreshOutputs(m);
    return true;
}
