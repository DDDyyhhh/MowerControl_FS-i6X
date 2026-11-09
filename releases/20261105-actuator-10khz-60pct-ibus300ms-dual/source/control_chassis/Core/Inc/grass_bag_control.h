#ifndef GRASS_BAG_CONTROL_H
#define GRASS_BAG_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GRASS_BAG_TARGET_UNKNOWN = 0,
    GRASS_BAG_TARGET_OPEN,
    GRASS_BAG_TARGET_CLOSED
} GrassBagTarget_t;

typedef struct {
    GrassBagTarget_t stable_target;
    GrassBagTarget_t candidate;
    uint8_t candidate_count;
    uint32_t last_frame;
    bool frame_seen;
} GrassBagRemote_t;

/* Only Motion calls this: denied input establishes no actionable baseline. */
bool GrassBagRemote_Advance(GrassBagRemote_t *state, uint16_t raw,
                            uint32_t frame, bool permitted,
                            GrassBagTarget_t *target);

typedef enum {
    GRASS_BAG_CLOSED = 0,
    GRASS_BAG_OPENING,
    GRASS_BAG_OPEN,
    GRASS_BAG_CLOSING,
    GRASS_BAG_STOPPED
} GrassBagState_t;

typedef struct {
    GrassBagState_t state;
    uint32_t started_ms;
    bool last_target_open;
    bool completed_open;
} GrassBagActuator_t;

#define GRASS_BAG_RUN_MS 20000U
bool GrassBagActuator_SetTarget(GrassBagActuator_t *bag, bool open, uint32_t now);
void GrassBagActuator_Stop(GrassBagActuator_t *bag);
bool GrassBagActuator_Advance(GrassBagActuator_t *bag, uint32_t now);
#endif
