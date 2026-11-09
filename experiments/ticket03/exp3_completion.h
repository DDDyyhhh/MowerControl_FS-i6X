#ifndef EXP3_COMPLETION_H
#define EXP3_COMPLETION_H
#include "exp3.h"
#if EXP3_ENABLED && MOWER_BLADE_OK_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
/* Three ordinary 32-bit SENS values. Each has an 8-bit publication ID and
 * three payload bytes. The nine-byte result retains the canonical CRC32. */
#define EXP3_COMPLETION_ADDRESS 13U
#define EXP3_COMPLETION_TYPE 0xE6U
#define EXP3_COMPLETION_PARTS 3U
#define EXP3_COMPLETION_BYTES 9U
#define EXP3_COMPLETION_FRESHNESS_MS 1000U
typedef struct {
    uint32_t started;
    uint8_t sequence, mask, bytes[EXP3_COMPLETION_BYTES];
} Exp3CompletionRx;
bool exp3_completion_encode(const Exp3Message *result, uint8_t bytes[EXP3_COMPLETION_BYTES]);
bool exp3_completion_decode(const uint8_t bytes[EXP3_COMPLETION_BYTES], uint32_t controller,
                            uint32_t hmi, Exp3Message *result);
uint32_t exp3_completion_part(const uint8_t bytes[EXP3_COMPLETION_BYTES], uint8_t sequence,
                              unsigned part);
int exp3_completion_receive(Exp3CompletionRx *rx, unsigned part, uint32_t word,
                            uint32_t now, uint32_t controller, uint32_t hmi,
                            Exp3Message *result);
#ifdef __cplusplus
}
#endif
#endif
#endif
