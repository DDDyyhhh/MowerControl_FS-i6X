#ifndef MOWER_HEIGHT_FEEDBACK_H
#define MOWER_HEIGHT_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

/* Read-only lifter estimate. Heartbeat advances only in the lifter task. */
#define MOWER_HEIGHT_SENS_ADDRESS    12U
#define MOWER_HEIGHT_SENS_TYPE       0xE4U
#define MOWER_HEIGHT_VALUE_MASK      0x7FU
#define MOWER_HEIGHT_SAMPLE_VALID    0x80U
#define MOWER_HEIGHT_MIN_MM          10U
#define MOWER_HEIGHT_MAX_MM          110U
#define MOWER_HEIGHT_HEARTBEAT_MASK  0xFFU
#define MOWER_HEIGHT_HEARTBEAT_MS    100U
#define MOWER_HEIGHT_FRESHNESS_MS    1000U

static inline bool mower_height_word_valid(uint16_t word)
{
    uint16_t mm = word & MOWER_HEIGHT_VALUE_MASK;
    return (word & MOWER_HEIGHT_SAMPLE_VALID) != 0U &&
           mm >= MOWER_HEIGHT_MIN_MM && mm <= MOWER_HEIGHT_MAX_MM;
}

#endif
