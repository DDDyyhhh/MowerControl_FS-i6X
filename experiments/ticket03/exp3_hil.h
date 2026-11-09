#ifndef EXP3_HIL_H
#define EXP3_HIL_H
#include "exp3_link.h"
#if EXP3_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
#ifndef EXP3_LOG_COUNT
#define EXP3_LOG_COUNT 16U
#endif
#define EXP3_ARM 0x45334152UL
#define EXP3_SEND 0x45335344UL
/* Debugger writes payload first, command last. No UI or actuator entry point. */
typedef struct {
    Exp3Message message;
    uint32_t command;
} Exp3Mailbox;
typedef struct {
    uint32_t tick, a, b, event;
} Exp3Event;
enum {
    E3_BIND = 1,
    E3_TX = 2,
    E3_RAW_RX = 3,
    E3_COMPLETE = 4,
    E3_REJECT = 5,
    E3_ID_SESSIONS = 6,
    E3_ID_ACTION = 7,
    E3_ID_CONTEXT = 8,
    E3_DECISION = 9,
    E3_DUPLICATE = 10,
    E3_TIMEOUT = 11,
    E3_RTT = 12,
    E3_BUSY = 13,
    E3_RAW_TX = 14,
    E3_CHANNELS = 15,
    E3_SENS_REQUEST = 16,
    E3_SENS_RESPONSE = 17
};
typedef struct {
    Exp3Link link;
    Exp3Message pending, last_result;
    uint32_t controller, hmi, rtt_ms, matches;
    volatile uint32_t log_write, log_read, log_overflow;
    volatile Exp3Event events[EXP3_LOG_COUNT];
    uint16_t published_word;
    uint8_t bound, waiting;
#if MOWER_BLADE_OK_ENABLED
    uint32_t request_started;
    uint32_t token_at, token_request;
    uint16_t token, authority;
    uint8_t token_active, token_armed, intent_reason;
    volatile uint8_t restart_requested;
    uint8_t cancel_retries, cancel_failed;
#endif
} Exp3Hil;
void exp3_hil_trace(Exp3Hil *h, uint32_t now, uint32_t event, uint32_t a, uint32_t b);
void exp3_hil_bind_session(Exp3Hil *h, Exp3Endpoint *endpoint, uint32_t controller,
                           uint32_t hmi, uint32_t now);
void exp3_hil_poll(Exp3Hil *h, volatile Exp3Mailbox *box, Exp3Endpoint *endpoint, uint32_t now,
                   uint32_t dwell);
void exp3_hil_channels(Exp3Hil *h, Exp3Endpoint *endpoint, uint16_t a, uint16_t b, uint32_t now);
void exp3_hil_receive(Exp3Hil *h, Exp3Endpoint *endpoint, uint16_t word, uint32_t now);
/* Only decoded, CRC-checked results enter this common matching boundary. */
bool exp3_hil_apply_result(Exp3Hil *h, const Exp3Message *result, uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
#endif
