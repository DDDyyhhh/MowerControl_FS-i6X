#ifndef MOWER_FAST_STATUS_H
#define MOWER_FAST_STATUS_H
#include <stdint.h>
#include "battery_status.h"
/* E9 profile 3: low half is profile-2 runtime; high half is Motion authority. */
#define MOWER_AUTH_TOKEN_MASK 0x0FFFU
#define MOWER_AUTH_UNLOCK 0x1000U
#define MOWER_AUTH_PHASE_SHIFT 13U
#define MOWER_AUTH_LIFETIME_MS 10000U
typedef enum {
    MOWER_AUTH_INIT = 0, MOWER_AUTH_SAFETY, MOWER_AUTH_STICKS,
    MOWER_AUTH_CH4, MOWER_AUTH_CENTER, MOWER_AUTH_CONFIRM,
    MOWER_AUTH_LOW, MOWER_AUTH_OWNER
} MowerAuthPhase;
static inline uint16_t mower_auth_pack(unsigned phase, unsigned unlock, unsigned token) {
    return (uint16_t)((phase << MOWER_AUTH_PHASE_SHIFT) |
        (unlock ? MOWER_AUTH_UNLOCK : 0U) | (token & MOWER_AUTH_TOKEN_MASK));
}
static inline unsigned mower_auth_phase(uint16_t word) { return word >> MOWER_AUTH_PHASE_SHIFT; }

#define MOWER_FAST_STATUS_TYPE 0xE9U
#define MOWER_FAST_STATUS_ADDRESS 10U
#define MOWER_FAST_VALID 0x08U
#define MOWER_FAST_LOCKED 0x10U
#define MOWER_FAST_RUN 0x20U
#define MOWER_FAST_COLLISION 0x40U
#define MOWER_FAST_FAULT 0x80U
#define MOWER_FAST_WHEEL_STALE 0x100U
#define MOWER_FAST_EMERGENCY 0x8000U
#define MOWER_FAST_STOP_MASK (MOWER_FAST_LOCKED | MOWER_FAST_COLLISION | MOWER_FAST_EMERGENCY)
/* E0 bits6/7 remain diagnostic/data validity; neither is a display alarm. */
#define MOWER_STATUS_ALARM_MASK 0xBF3FU /* bit14 reserved; not an alarm */
#define MOWER_FAST_HEARTBEAT_MASK 0x3FU
#define MOWER_FAST_HEARTBEAT_SHIFT 9U
#define MOWER_FAST_HEARTBEAT_WORD_MASK (MOWER_FAST_HEARTBEAT_MASK << MOWER_FAST_HEARTBEAT_SHIFT)
#define MOWER_FAST_HEARTBEAT_FORWARD_LIMIT ((MOWER_FAST_HEARTBEAT_MASK + 1U) / 2U)
#define MOWER_FAST_FRESHNESS_MS 1000U
#define MOWER_FAST_HEARTBEAT_MS 100U
static inline uint16_t mower_fast_status_pack(uint8_t blade, uint16_t faults,
                                             uint8_t run, uint32_t now)
{
    uint16_t flags = MOWER_FAST_VALID;
    if (faults & 0x400U) flags |= MOWER_FAST_LOCKED;
    if (faults & 1U) flags |= MOWER_FAST_EMERGENCY | MOWER_FAST_LOCKED;
    if (faults & 2U) flags |= MOWER_FAST_COLLISION;
    if (faults & 0x80U) flags |= MOWER_FAST_WHEEL_STALE;
    if (faults & MOWER_STATUS_ALARM_MASK & (uint16_t)~0x402U) flags |= MOWER_FAST_FAULT;
    if (run && !(flags & MOWER_FAST_STOP_MASK)) {
        flags |= MOWER_FAST_RUN;
        flags |= blade & 7U;
    }
    return (uint16_t)((((now / MOWER_FAST_HEARTBEAT_MS) & MOWER_FAST_HEARTBEAT_MASK) << MOWER_FAST_HEARTBEAT_SHIFT) | flags);
}
#endif
