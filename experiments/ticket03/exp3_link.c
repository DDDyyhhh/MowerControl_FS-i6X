#include "exp3_link.h"
#if EXP3_ENABLED
#include <string.h>
#include "exp3_probe.h"
#define FAST_RESULT_WORDS (MOWER_BLADE_OK_ENABLED ? 18U : 17U)
#define FAST_RESULT_CRC_OFFSET (MOWER_BLADE_OK_ENABLED ? 14U : 13U)
void exp3_link_init(Exp3Link *l) { memset(l, 0, sizeof *l); }
bool exp3_link_send(Exp3Link *l, const Exp3Message *m, uint32_t now) {
    if (l->tx_active || l->transfer == UINT32_MAX || !exp3_encode(m, l->tx))
        return false;
    ++l->transfer;
    l->tx_index = 0U;
    l->tx_active = 1U;
    l->tx_fast = 0U;
    l->sent_at = l->word_at = now;
#if MOWER_BLADE_OK_ENABLED
    if (m->kind >= EXP3_CONFIRM && m->kind <= EXP3_QUERY &&
        (m->context == EXP3_PWM_CONTEXT || m->context == EXP3_UNLOCK_CONTEXT) &&
        m->target == EXP3_PWM_TARGET && m->request == m->action &&
        m->action <= 65535U && m->generation <= 65535U) {
        /* Forward movement is intentionally overlap-safe. Expand the complete
         * canonical message before checking its unchanged full-message CRC32. */
        /* Session identities are implicit, but remain covered by canonical CRC32. */
        memcpy(l->tx, l->tx + 16, 2U);
        memcpy(l->tx + 2, l->tx + 24, 2U);
        l->tx[4] = (uint8_t)(m->kind | (m->context == EXP3_UNLOCK_CONTEXT ? 4U : 0U));
        memcpy(l->tx + 5, l->tx + 32, 4U);
        l->tx_fast = 2U;
        return true;
    }
#endif
    return exp3_fragment(l->tx, l->transfer, 0, l->fragment);
}

bool exp3_link_send_result(Exp3Link *l, const Exp3Message *m, uint32_t now) {
    uint8_t full[EXP3_MESSAGE_BYTES];
    if (!m || m->kind != EXP3_RESULT ||
        (m->context != EXP3_PWM_CONTEXT &&
         (!MOWER_BLADE_OK_ENABLED || m->context != EXP3_UNLOCK_CONTEXT)) ||
        m->target != EXP3_PWM_TARGET || m->request != m->action ||
        m->action > 65535U || m->generation > 65535U)
        return exp3_link_send(l, m, now);
    if (l->tx_active || l->transfer == UINT32_MAX || !exp3_encode(m, full))
        return false;
    /* Bench-only lossless compression: expand the fixed context/target and
     * request==action before checking the original full-message CRC32. */
    memcpy(l->tx, full + 4, 8U);
    memcpy(l->tx + 8, full + 16, 2U);
    memcpy(l->tx + 10, full + 24, 2U);
    l->tx[12] = (uint8_t)(m->status | (m->command << 4));
    if (MOWER_BLADE_OK_ENABLED)
        l->tx[13] = m->context == EXP3_UNLOCK_CONTEXT ? 1U : 0U;
    memcpy(l->tx + FAST_RESULT_CRC_OFFSET, full + 32, 4U);
    ++l->transfer;
    l->tx_index = 0U;
    l->tx_active = l->tx_fast = 1U;
    l->sent_at = l->word_at = now;
    return true;
}
uint16_t exp3_link_word(Exp3Link *l, uint32_t now, uint32_t dwell) {
    unsigned slot;
    if (!l->tx_active || !dwell)
        return 0;
    if ((uint32_t)(now - l->word_at) >= dwell) {
        /* Never skip words after scheduler delays. Measured latency includes them. */
        l->word_at = now;
        if (++l->tx_index == (l->tx_fast == 2U ? 9U : l->tx_fast ? FAST_RESULT_WORDS : 90U)) {
            l->tx_active = 0;
            return 0;
        }
        if (!l->tx_fast && l->tx_index % 18 == 0)
            (void)exp3_fragment(l->tx, l->transfer, l->tx_index / 18, l->fragment);
    }
    if (l->tx_fast)
        return (uint16_t)((l->tx_fast == 2U ? 0x8000U : 0xe000U) |
                          ((uint16_t)l->tx_index << 8) | l->tx[l->tx_index]);
    slot = l->tx_index % 18;
    return (uint16_t)(0xa000U | (slot << 8) | l->fragment[slot]);
}
static int receive_fast_result(Exp3Link *l, uint16_t w, uint32_t now,
                                uint32_t timeout, Exp3Message *out) {
    unsigned slot = (w >> 8) & 31U;
    uint8_t mode = (w & 0xe000U) == 0x8000U ? 2U : 1U;
    uint8_t full[EXP3_MESSAGE_BYTES] = {0};
    if (!timeout || !out || slot >= (mode == 2U ? 9U : FAST_RESULT_WORDS)) {
        l->raw_active = 0U;
        return EXP3_REJECT;
    }
    if (l->raw_active && (l->rx_fast != mode || (uint32_t)(now - l->raw_started) >= timeout))
        l->raw_active = 0U;
    if (w == l->last_word)
        return EXP3_DUPLICATE;
    l->last_word = w;
    if (slot == 0U) {
        l->raw_active = 1U;
        l->rx_fast = mode;
        l->raw_next = 0U;
        l->raw_started = now;
        l->rx.active = 0U;
    }
    if (!l->raw_active || l->rx_fast != mode || slot != l->raw_next) {
        l->raw_active = 0U;
        return EXP3_REJECT;
    }
    l->raw[slot] = (uint8_t)w;
    if (++l->raw_next != (mode == 2U ? 9U : FAST_RESULT_WORDS))
        return EXP3_MORE;
    l->raw_active = 0U;
    full[0] = 0xe3U;
    full[1] = EXP3_WIRE_VERSION;
    full[2] = EXP3_RESULT;
    full[3] = 2U;
    if (mode == 2U) {
        uint8_t flags = l->raw[4];
        uint32_t context = flags & 4U ? EXP3_UNLOCK_CONTEXT : EXP3_PWM_CONTEXT;
        if (!l->controller || !l->hmi || (flags & 0xf8U) || !(flags & 3U))
            return EXP3_REJECT;
        full[2] = flags & 3U; full[3] = 1U;
        memcpy(full + 4, &l->controller, 8U);
        memcpy(full + 12, l->raw, 2U);
        memcpy(full + 16, l->raw, 2U);
        memcpy(full + 20, &context, 4U);
        memcpy(full + 24, l->raw + 2, 2U);
        full[28] = EXP3_PWM_TARGET;
        memcpy(full + 32, l->raw + 5, 4U);
    } else {
    memcpy(full + 4, l->raw, 8U);
    memcpy(full + 12, l->raw + 8, 2U);
    memcpy(full + 16, l->raw + 8, 2U);
    full[20] = (uint8_t)EXP3_PWM_CONTEXT;
    full[21] = (uint8_t)(EXP3_PWM_CONTEXT >> 8);
    full[22] = (uint8_t)(EXP3_PWM_CONTEXT >> 16);
    full[23] = (uint8_t)(EXP3_PWM_CONTEXT >> 24);
    if (MOWER_BLADE_OK_ENABLED) {
        uint32_t context;
        if (l->raw[13] > 1U) return EXP3_REJECT;
        context = l->raw[13] ? EXP3_UNLOCK_CONTEXT : EXP3_PWM_CONTEXT;
        full[20] = (uint8_t)context;
        full[21] = (uint8_t)(context >> 8);
        full[22] = (uint8_t)(context >> 16);
        full[23] = (uint8_t)(context >> 24);
    }
    memcpy(full + 24, l->raw + 10, 2U);
    full[28] = EXP3_PWM_TARGET;
    full[30] = l->raw[12] & 15U;
    full[31] = l->raw[12] >> 4;
    memcpy(full + 32, l->raw + FAST_RESULT_CRC_OFFSET, 4U);
    }
    return exp3_decode(full, sizeof(full), out) ? EXP3_COMPLETE : EXP3_REJECT;
}
int exp3_link_receive(Exp3Link *l, uint16_t w, uint32_t now, uint32_t timeout, Exp3Message *out) {
    unsigned slot = (w >> 8) & 31;
    int result;
    (void)exp3_expire(&l->rx, now, timeout);
    if ((w & 0xe000U) == 0xe000U ||
        (MOWER_BLADE_OK_ENABLED && (w & 0xe000U) == 0x8000U))
        return receive_fast_result(l, w, now, timeout, out);
    if (l->raw_active && l->rx_fast)
        l->raw_active = 0U;
    l->rx_fast = 0U;
    if (l->raw_active && (uint32_t)(now - l->raw_started) >= timeout)
        l->raw_active = 0;
    if ((w & 0xe000U) != 0xa000U || slot >= 18 || !timeout) {
        l->raw_active = 0;
        l->rx.active = 0;
        return EXP3_REJECT;
    }
    if (w == l->last_word)
        return EXP3_DUPLICATE;
    l->last_word = w;
    if (slot == 0) {
        l->raw_active = 1;
        l->raw_next = 0;
        l->raw_started = now;
    }
    if (!l->raw_active || slot != l->raw_next) {
        l->raw_active = 0;
        l->rx.active = 0;
        return EXP3_REJECT;
    }
    l->raw[slot] = (uint8_t)w;
    ++l->raw_next;
    if (l->raw_next != 18)
        return EXP3_MORE;
    l->raw_active = 0;
    result = exp3_receive(&l->rx, l->raw, 18, now, timeout, out);
    return result;
}
void exp3_channels(uint16_t w, uint16_t *a, uint16_t *b) {
    *a = (uint16_t)(1000U + 3U * (w & 255));
    *b = (uint16_t)(1000U + 3U * (w >> 8));
}
bool exp3_channel_raw_word(uint16_t a, uint16_t b, uint16_t *w) {
    if (a < 1000 || a > 1765 || b < 1000 || b > 1765 || (a - 1000) % 3 || (b - 1000) % 3)
        return false;
    *w = (uint16_t)((a - 1000) / 3 | (((b - 1000) / 3) << 8));
    return true;
}
bool exp3_channel_word(uint16_t a, uint16_t b, uint16_t *w) {
    if (!exp3_channel_raw_word(a, b, w))
        return false;
    return (((*w & 0xe000U) == 0xa000U) ||
            (MOWER_BLADE_OK_ENABLED && (*w & 0xe000U) == 0x8000U)) &&
           ((*w >> 8) & 31) < 18;
}
#endif
