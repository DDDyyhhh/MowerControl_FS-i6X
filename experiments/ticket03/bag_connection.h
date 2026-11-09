#ifndef MOWER_BAG_CONNECTION_H
#define MOWER_BAG_CONNECTION_H

#include <stdint.h>

/* Read-only PD10 telemetry; never a position or motion authorization. */
#define MOWER_BAG_SENS_ADDRESS       11U
#define MOWER_BAG_SENS_TYPE          0xE3U
#define MOWER_BAG_CONNECTED         1U
#define MOWER_BAG_SAMPLE_VALID      2U
#define MOWER_BAG_HEARTBEAT_MASK    0x3FFFU
#define MOWER_BAG_HEARTBEAT_MS      100U
#define MOWER_BAG_FRESHNESS_MS      1000U

#endif
