#ifndef TICKET03_EXP3_H
#define TICKET03_EXP3_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef EXP3_ENABLED
#define EXP3_ENABLED 0
#endif
#ifndef MOWER_BLADE_OK_ENABLED
#define MOWER_BLADE_OK_ENABLED 0
#endif
#define EXP3_WIRE_VERSION (MOWER_BLADE_OK_ENABLED ? 0x36U : 0x31U)
#if EXP3_ENABLED != 0 && EXP3_ENABLED != 1
#error "EXP3_ENABLED must be 0 or 1"
#endif
#if EXP3_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
#define EXP3_MESSAGE_BYTES 36U
#define EXP3_FRAGMENT_BYTES 18U
#define EXP3_FRAGMENT_COUNT 5U
#define EXP3_CACHE_SIZE 4U
/* Experiment only: fresh nonzero paired sessions; base uses fixture, PWM bench uses wire boot exchange. */
enum { EXP3_CONFIRM = 1, EXP3_CANCEL = 2, EXP3_QUERY = 3, EXP3_RESULT = 4 };
enum {
    EXP3_NONE = 0,
    EXP3_ACCEPTED = 1,
    EXP3_CANCELLED = 2,
    EXP3_UNKNOWN = 3,
    EXP3_STALE = 4,
    EXP3_CONFLICT = 5,
    EXP3_SESSION = 6
};
enum { EXP3_REJECT = -1, EXP3_MORE = 0, EXP3_COMPLETE = 1, EXP3_DUPLICATE = 2 };
typedef struct {
    uint32_t controller, hmi, request, action, context, generation;
    uint16_t target;
    uint8_t kind, sender, status, command;
} Exp3Message;
typedef struct {
    Exp3Message records[EXP3_CACHE_SIZE];
    uint32_t controller, hmi, high_action, created;
    uint8_t next;
} Exp3Endpoint;
void exp3_bind(Exp3Endpoint *e, uint32_t controller, uint32_t hmi);
bool exp3_decide(Exp3Endpoint *e, const Exp3Message *m, Exp3Message *result);
typedef struct {
    uint32_t transfer, started;
    uint8_t bytes[40];
    uint8_t next, active;
} Exp3Rx;
bool exp3_fragment(const uint8_t bytes[EXP3_MESSAGE_BYTES], uint32_t transfer, unsigned index,
                   uint8_t out[EXP3_FRAGMENT_BYTES]);
int exp3_receive(Exp3Rx *rx, const uint8_t *fragment, size_t length, uint32_t now, uint32_t timeout,
                 Exp3Message *out);
bool exp3_expire(Exp3Rx *rx, uint32_t now, uint32_t timeout);
bool exp3_encode(const Exp3Message *m, uint8_t out[EXP3_MESSAGE_BYTES]);
bool exp3_decode(const uint8_t *bytes, size_t length, Exp3Message *m);
#ifdef __cplusplus
}
#endif
#endif
#endif
