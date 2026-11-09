#ifndef EXP3_LINK_H
#define EXP3_LINK_H
#include "exp3.h"
#if EXP3_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
/* Single writer per link. Caller supplies an experiment-only deadline. */
typedef struct {
    Exp3Rx rx;
    uint32_t controller, hmi; /* Trusted session for compact uplink reconstruction. */
    uint32_t transfer, sent_at, word_at, raw_started;
    uint8_t tx[36], fragment[18], raw[18];
    uint16_t last_word;
    uint8_t tx_index, tx_active, raw_next, raw_active;
    uint8_t tx_fast, rx_fast;
} Exp3Link;
void exp3_link_init(Exp3Link *l);
bool exp3_link_send(Exp3Link *l, const Exp3Message *m, uint32_t now);
bool exp3_link_send_result(Exp3Link *l, const Exp3Message *m, uint32_t now);
uint16_t exp3_link_word(Exp3Link *l, uint32_t now, uint32_t dwell);
int exp3_link_receive(Exp3Link *l, uint16_t word, uint32_t now, uint32_t timeout, Exp3Message *out);
void exp3_channels(uint16_t word, uint16_t *ch11, uint16_t *ch12);
bool exp3_channel_word(uint16_t ch11, uint16_t ch12, uint16_t *word);
bool exp3_channel_raw_word(uint16_t ch11, uint16_t ch12, uint16_t *word);
#ifdef __cplusplus
}
#endif
#endif
#endif
