#include "grass_bag_control.h"

#include <string.h>

bool GrassBagRemote_Advance(GrassBagRemote_t *state, uint16_t raw,
                            uint32_t frame, bool permitted,
                            GrassBagTarget_t *target)
{
    GrassBagTarget_t position = GRASS_BAG_TARGET_UNKNOWN;
    if (!permitted) {
        memset(state, 0, sizeof(*state));
        /* Do not count the frame seen while disallowed after rearming. */
        state->last_frame = frame;
        state->frame_seen = true;
        return false;
    }
    if (state->frame_seen && state->last_frame == frame) return false;
    state->frame_seen = true;
    state->last_frame = frame;
    if (raw >= 880U && raw <= 1120U) position = GRASS_BAG_TARGET_OPEN;
    if (raw >= 1880U && raw <= 2120U) position = GRASS_BAG_TARGET_CLOSED;
    if (position == GRASS_BAG_TARGET_UNKNOWN) {
        state->candidate = GRASS_BAG_TARGET_UNKNOWN;
        state->candidate_count = 0U;
        return false;
    }
    if (state->candidate != position) {
        state->candidate = position;
        state->candidate_count = 1U;
    } else if (state->candidate_count < 3U) {
        state->candidate_count++;
    }
    if (state->candidate_count < 3U || position == state->stable_target) return false;
    bool baseline = state->stable_target == GRASS_BAG_TARGET_UNKNOWN;
    state->stable_target = position;
    *target = position;
    return !baseline;
}

bool GrassBagActuator_SetTarget(GrassBagActuator_t *bag, bool open, uint32_t now)
{
    if ((open && (bag->state == GRASS_BAG_OPEN || bag->state == GRASS_BAG_OPENING)) ||
        (!open && (bag->state == GRASS_BAG_CLOSED || bag->state == GRASS_BAG_CLOSING)))
        return false;
    bag->state = open ? GRASS_BAG_OPENING : GRASS_BAG_CLOSING;
    bag->last_target_open = open;
    bag->started_ms = now;
    return true;
}

void GrassBagActuator_Stop(GrassBagActuator_t *bag)
{
    if (bag->state == GRASS_BAG_OPENING || bag->state == GRASS_BAG_CLOSING) {
        bag->state = GRASS_BAG_STOPPED;
        bag->started_ms = 0U;
    }
}

bool GrassBagActuator_Advance(GrassBagActuator_t *bag, uint32_t now)
{
    if ((bag->state != GRASS_BAG_OPENING && bag->state != GRASS_BAG_CLOSING) ||
        (uint32_t)(now - bag->started_ms) < GRASS_BAG_RUN_MS) return false;
    bag->completed_open = bag->state == GRASS_BAG_OPENING;
    bag->state = bag->completed_open ? GRASS_BAG_OPEN : GRASS_BAG_CLOSED;
    return true;
}
