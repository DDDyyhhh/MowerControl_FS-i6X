#ifndef TICKET03_EXP3_AUTO_SESSION_H
#define TICKET03_EXP3_AUTO_SESSION_H

#include "exp3.h"

#if EXP3_ENABLED
#ifdef __cplusplus
extern "C" {
#endif

enum { EXP3_AUTO_HMI = 0, EXP3_AUTO_CONTROLLER = 1 };

typedef struct {
    uint32_t self, peer, rx_started, word_at, last_round;
    uint16_t last_word;
    uint8_t rx_bytes[4];
    uint8_t role, rx_next, tx_index, tx_active;
} Exp3AutoSession;

/* Handshake pacing: retry fast only while a session is actually missing. */
#define EXP3_AUTO_ROUND_MS 2000U
#define EXP3_AUTO_HMI_FAST_ROUND_MS 400U
#define EXP3_AUTO_CONTROLLER_FAST_ROUND_MS 500U
#define EXP3_AUTO_CONTROLLER_FAST_WINDOW_MS 6000U

typedef struct {
    uint32_t bound_at;
    uint8_t settled, link_was_fresh;
} Exp3AutoPace;

uint32_t exp3_auto_hmi_interval(Exp3AutoPace *pace, bool bound, bool link_fresh);
void exp3_auto_hmi_settled(Exp3AutoPace *pace);
void exp3_auto_controller_bound(Exp3AutoPace *pace, uint32_t now);
uint32_t exp3_auto_controller_interval(Exp3AutoPace *pace, uint32_t now);

void exp3_auto_init(Exp3AutoSession *session, uint8_t role, uint32_t nonce, uint32_t now);
uint16_t exp3_auto_word(Exp3AutoSession *session, uint32_t now, uint32_t dwell,
                        uint32_t interval);
/* Returns 1 only for a newly completed peer identity. CRC detects corruption, not forgery. */
int exp3_auto_receive(Exp3AutoSession *session, uint16_t word, uint32_t now,
                      uint32_t timeout);
bool exp3_auto_is_word(uint16_t word);

#ifdef __cplusplus
}
#endif
#endif
#endif
