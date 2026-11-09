#include "mower_mode_control.h"
#include <string.h>

static void invalidate(MowerModeControl_t *s)
{
    ++s->invalidation_sequence;
    s->stop_requested = true;
    s->ready = false;
    s->center_tracking = false;
    if (s->generation == UINT16_MAX) {
        s->generation = 0U;
        s->generation_exhausted = true;
        s->armed = s->permitted = false;
    } else if (!s->generation_exhausted) ++s->generation;
}
void MowerMode_Init(MowerModeControl_t *s)
{
    memset(s, 0, sizeof(*s));
    s->generation = 1U;
    s->latch_reason = REMOTE_LATCH_REASON_SYNCHRONIZING;
    s->candidate = 255U;
}
void MowerMode_Stop(MowerModeControl_t *s)
{
    if (s->armed || s->ready || s->center_tracking || s->api_mode) {
        invalidate(s); s->latch_reason=REMOTE_LATCH_REASON_REMOTE_STOP;
    }
    s->armed = s->permitted = s->ready = s->center_tracking = false;
    s->api_mode = 0U;
}
void MowerMode_Session(MowerModeControl_t *s, uint32_t controller, uint32_t hmi)
{
    if (s->controller == controller && s->hmi == hmi) return;
    MowerMode_Stop(s);
    invalidate(s);
    s->latch_reason=REMOTE_LATCH_REASON_SYNCHRONIZING;
    s->controller = controller; s->hmi = hmi;
    s->session_bound = controller != 0U && hmi != 0U;
    if (s->session_bound && s->generation_exhausted) {
        s->generation_exhausted = false;
        s->generation = 1U;
    }
}
static bool near(uint16_t a, uint16_t b)
{
    return a >= b - REMOTE_SWITCH_POSITION_TOLERANCE_US &&
           a <= b + REMOTE_SWITCH_POSITION_TOLERANCE_US;
}
void MowerMode_Advance(MowerModeControl_t *s, const MowerModeInput_t *in, uint32_t now)
{
    bool fresh = in->frame_sequence != s->frame_sequence;
    uint8_t mode = near(in->mode_raw, 2000U) ? MOWER_SELECT_API :
        (near(in->mode_raw, 1000U) || near(in->mode_raw, 1500U)) ? MOWER_SELECT_RC : 255U;
    s->online = in->online; s->enabled = in->enabled;
    if (!in->online || mode==255U) {
        MowerMode_Stop(s);
        s->latch_reason=!in->online ? REMOTE_LATCH_REASON_TIMEOUT : REMOTE_LATCH_REASON_OWNERSHIP_TRANSITION;
        if (s->mode_valid) { invalidate(s); ++s->mode_sequence; }
        s->mode_valid=false; s->candidate_count=0U;
        s->frame_sequence=in->frame_sequence;
        return;
    }
    if (fresh) {
        s->frame_sequence = in->frame_sequence;
        if (mode != s->candidate) { s->candidate = mode; s->candidate_count = 1U; }
        else if (s->candidate_count < REMOTE_SWITCH_CONFIRM_FRAMES) ++s->candidate_count;
        if (s->candidate_count >= REMOTE_SWITCH_CONFIRM_FRAMES &&
            (!s->mode_valid || mode != s->selected)) {
            s->selected = (MowerSelectedMode_t)mode; s->mode_valid = true;
            s->api_mode = 0U;
            s->permitted = false; s->armed_frame = in->frame_sequence;
            ++s->mode_sequence;
            invalidate(s); /* A normal handoff deliberately preserves armed. */
        }
        if (s->armed && in->frame_sequence != s->armed_frame) s->permitted = true;
    }
    /* Mode selection remains observable while stopped or in a safety latch. */
    if (!in->enabled || in->safety_active || !s->session_bound || s->generation_exhausted) {
        MowerMode_Stop(s);
        s->latch_reason=in->safety_active ? REMOTE_LATCH_REASON_SAFETY_REJECT :
            !s->session_bound ? REMOTE_LATCH_REASON_SYNCHRONIZING :
            !in->enabled ? REMOTE_LATCH_REASON_REMOTE_STOP : REMOTE_LATCH_REASON_OWNERSHIP_TRANSITION;
        return;
    }
    if (!s->mode_valid) return;
    if (s->api_mode && (!in->centered ||
        (s->api_mode == 2U && (uint32_t)(now-s->host_tick) >= 500U))) {
        MowerMode_Stop(s);
        s->latch_reason=!in->centered ? REMOTE_LATCH_REASON_OWNERSHIP_TRANSITION : REMOTE_LATCH_REASON_TIMEOUT;
        return;
    }
    if (!s->armed && in->centered && in->blade_centered) {
        if (!s->center_tracking) { s->center_tracking = true; s->center_started = now; }
        s->ready = (uint32_t)(now-s->center_started) >= REMOTE_STICK_CENTER_HOLD_MS;
    } else {
        if (!s->armed) MowerMode_Stop(s); /* Retire an OK when its prerequisites disappear. */
        s->ready = false; s->center_tracking = false;
    }
}
bool MowerMode_Confirm(MowerModeControl_t *s, uint32_t generation)
{
    if (!s->ready || !s->session_bound || !s->mode_valid || s->armed ||
        s->generation_exhausted || !generation || generation != s->generation) return false;
    s->armed = true; s->permitted = s->ready = false;
    s->latch_reason=REMOTE_LATCH_REASON_NONE;
    s->armed_frame = s->frame_sequence;
    ++s->invalidation_sequence;
    s->stop_requested = true;
    return true;
}
bool MowerMode_Cancel(MowerModeControl_t *s, uint32_t generation)
{
    if (!generation || generation != s->generation) return false;
    MowerMode_Stop(s);
    /* Cancellation of an unarmed prompt must also retire its identity. */
    if (generation == s->generation) invalidate(s);
    return true;
}
bool MowerMode_Host(MowerModeControl_t *s, uint8_t command, uint32_t tick)
{
    if (!s->mode_valid || s->selected != MOWER_SELECT_API || !s->permitted) return false;
    if (command == 0U) { if (s->api_mode) MowerMode_Stop(s); return false; }
    if (command == 1U) { s->api_mode = 1U; s->host_tick = tick; return false; }
    if (command == 2U && (s->api_mode == 1U || s->api_mode == 2U)) {
        s->api_mode = 2U; s->host_tick = tick; return true;
    }
    return false;
}
uint16_t MowerMode_Status(const MowerModeControl_t *s, uint32_t now)
{
    return (uint16_t)((((now/100U) & MOWER_CONTROL_HEARTBEAT_MASK) << 6) |
        ((s->mode_valid && s->online && s->session_bound) ? MOWER_CONTROL_VALID : 0U) | (s->armed ? MOWER_CONTROL_ARMED : 0U) |
        (s->ready ? MOWER_CONTROL_READY : 0U) |
        (s->selected == MOWER_SELECT_API ? MOWER_CONTROL_API_SELECTED : 0U) |
        ((uint16_t)s->api_mode << MOWER_CONTROL_MODE_SHIFT));
}
