#include "exp3_auto_session.h"

#if EXP3_ENABLED
#include <string.h>

static uint8_t checksum(const uint8_t bytes[4], uint8_t role)
{
    uint8_t value = (uint8_t)(0x5aU ^ role);
    unsigned i, bit;
    for (i = 0U; i < 4U; ++i) {
        value ^= bytes[i];
        for (bit = 0U; bit < 8U; ++bit)
            value = (uint8_t)((value << 1) ^ ((value & 0x80U) ? 0x07U : 0U));
    }
    return value;
}

bool exp3_auto_is_word(uint16_t word)
{
    return (word & 0xe000U) == 0xc000U && ((word >> 8) & 7U) < 5U;
}

void exp3_auto_init(Exp3AutoSession *session, uint8_t role, uint32_t nonce, uint32_t now)
{
    memset(session, 0, sizeof(*session));
    session->role = role;
    session->self = nonce ? nonce : 1U;
    session->word_at = now;
    session->last_round = now;
    session->tx_active = role == EXP3_AUTO_HMI;
}

uint16_t exp3_auto_word(Exp3AutoSession *session, uint32_t now, uint32_t dwell,
                        uint32_t interval)
{
    uint8_t bytes[4], value;
    unsigned i;
    if (!session || !dwell || !interval ||
        (session->role == EXP3_AUTO_CONTROLLER && !session->peer))
        return 0U;
    if (!session->tx_active) {
        if ((uint32_t)(now - session->last_round) < interval)
            return 0U;
        session->tx_active = 1U;
        session->tx_index = 0U;
        session->word_at = now;
    } else if ((uint32_t)(now - session->word_at) >= dwell) {
        session->word_at = now;
        if (++session->tx_index == 5U) {
            session->tx_active = 0U;
            session->last_round = now;
            return 0U;
        }
    }
    for (i = 0U; i < 4U; ++i)
        bytes[i] = (uint8_t)(session->self >> (8U * i));
    value = session->tx_index < 4U ? bytes[session->tx_index] :
            checksum(bytes, session->role);
    return (uint16_t)(0xc000U | ((uint16_t)session->role << 11) |
                      ((uint16_t)session->tx_index << 8) | value);
}

int exp3_auto_receive(Exp3AutoSession *session, uint16_t word, uint32_t now,
                      uint32_t timeout)
{
    uint8_t slot, peer_role;
    uint32_t peer;
    if (!session || !timeout || !exp3_auto_is_word(word))
        return 0;
    peer_role = (uint8_t)((word >> 11) & 1U);
    if (peer_role == session->role)
        return 0;
    if (session->rx_next && (uint32_t)(now - session->rx_started) >= timeout)
        session->rx_next = 0U;
    if (word == session->last_word)
        return 0;
    session->last_word = word;
    slot = (uint8_t)((word >> 8) & 7U);
    if (slot == 0U) {
        session->rx_next = 0U;
        session->rx_started = now;
    }
    if (slot != session->rx_next)
        return 0;
    if (slot < 4U) {
        session->rx_bytes[slot] = (uint8_t)word;
        ++session->rx_next;
        return 0;
    }
    session->rx_next = 0U;
    if ((uint8_t)word != checksum(session->rx_bytes, peer_role))
        return 0;
    peer = (uint32_t)session->rx_bytes[0] |
           ((uint32_t)session->rx_bytes[1] << 8) |
           ((uint32_t)session->rx_bytes[2] << 16) |
           ((uint32_t)session->rx_bytes[3] << 24);
    if (!peer || peer == session->peer)
        return 0;
    session->peer = peer;
    if (session->role == EXP3_AUTO_CONTROLLER) {
        session->tx_active = 1U;
        session->tx_index = 0U;
        session->word_at = now;
    }
    return 1;
}
#endif
