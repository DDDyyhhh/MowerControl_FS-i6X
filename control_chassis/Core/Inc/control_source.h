#ifndef CONTROL_SOURCE_H
#define CONTROL_SOURCE_H
#include <stdbool.h>
#include <stdint.h>

/* Distinct from cfg_op_mode (mowing behaviour) and from safety authorization. */
enum { CONTROL_SOURCE_UNKNOWN, CONTROL_SOURCE_REMOTE, CONTROL_SOURCE_API };
enum { CONTROL_MODE_REMOTE, CONTROL_MODE_API_STANDBY, CONTROL_MODE_API_RUN };
enum { CONTROL_DISPLAY_UNKNOWN, CONTROL_DISPLAY_REMOTE, CONTROL_DISPLAY_API_STANDBY,
       CONTROL_DISPLAY_API_RUN, CONTROL_DISPLAY_TAKEOVER };
#define CONTROL_API_TIMEOUT_MS 500U

static inline uint8_t ControlSource_DecodeCh8(uint16_t value)
{
    if ((value >= 900U && value <= 1120U) ||
        (value >= 1380U && value <= 1620U)) return CONTROL_SOURCE_REMOTE;
    if (value >= 1880U && value <= 2100U) return CONTROL_SOURCE_API;
    return CONTROL_SOURCE_UNKNOWN;
}

/* CANRx is the single publisher. Event sequences survive overwritten snapshots.
 * Zero means no event; the publisher skips zero when its sequence wraps. */
typedef struct {
    uint32_t sequence, received_at_ms;
    uint32_t standby_sequence, standby_at_ms, halt_sequence, exit_sequence;
    int8_t vx, omega;
    uint8_t mode, blade, height, bag;
    bool valid;
} HostControlSnapshot_t;

typedef struct {
    uint32_t now_ms, frame_sequence, frame_at_ms;
    uint16_t ch8, steering, throttle;
    bool link_ok, authorized;
    uint32_t mode_events;
} ControlSourceInput_t;

/* Owned exclusively by Motion. stop/revoke are outputs for the current tick. */
typedef struct {
    uint8_t requested, candidate, candidate_count, mode, display;
    bool frame_seen, valid, takeover, center_tracking, remote_ready, api_ready;
    bool stop, revoke, was_authorized;
    uint32_t last_frame, center_since, barrier_ms, barrier_sequence;
    uint32_t last_host_sequence, last_halt_sequence, activation_sequence;
    uint32_t invalidation_sequence, last_mode_events;
    HostControlSnapshot_t command;
} ControlSource_t;

bool HostControl_Publish(HostControlSnapshot_t *snapshot, const uint8_t *data,
                         uint8_t length, uint32_t received_at_ms);
void ControlSource_Reset(ControlSource_t *state);
void ControlSource_Advance(ControlSource_t *state, const ControlSourceInput_t *input,
                           const HostControlSnapshot_t *host);
bool ControlSource_CanUnlock(const ControlSource_t *state);
#endif
