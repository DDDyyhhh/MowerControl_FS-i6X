#ifndef MOWER_BATTERY_STATUS_H
#define MOWER_BATTERY_STATUS_H
#include <stdint.h>

/* Additive E0 bits; addresses 1..12 and all existing units stay unchanged. */
#define MOWER_BATTERY_WARNING       0x0800U
#define MOWER_BATTERY_BLADE_INHIBIT 0x1000U /* LOW latch; never limits travel */
#define MOWER_BATTERY_STOP          0x2000U
/* E0 bit14 is reserved; no calibration status is published. */
#define MOWER_BATTERY_STATUS_MASK   0x3800U
/* Diagnostic words use only idle E1/addr9 polls. Addresses 13..15/E6..E8
 * belong exclusively to fast completion. Never interrupt a result/session.
 * Two 256ms phases: exact mV (13 bits), then reasons (8 bits) + heartbeat.
 * This is display-only; neither word is a command or an authorization. */
#define MOWER_BATTERY_DIAG_MIN       0x4000U
#define MOWER_BATTERY_DIAG_REASON    0x6000U
#define MOWER_BATTERY_DIAG_KIND      0xE000U
#define MOWER_BATTERY_DIAG_FRESH_MS  3000U
static inline uint16_t mower_battery_diag_word(uint16_t minimum, uint16_t reasons,
                                               uint32_t sample_at)
{
    if (sample_at & 0x100U)
        return (uint16_t)(MOWER_BATTERY_DIAG_REASON |
            (((sample_at >> 9U) & 31U) << 8U) | (reasons & 0xFFU));
    return (uint16_t)(MOWER_BATTERY_DIAG_MIN | (minimum <= 5000U ? minimum : 0U));
}

#endif
