#include "exp3.h"
#if EXP3_ENABLED
#include <string.h>
#if MOWER_BLADE_OK_ENABLED
#include "exp3_completion.h"
#include "exp3_probe.h"
#endif
static void put32(uint8_t *p, uint32_t v) {
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* CRC-32/ISO-HDLC: corruption detection, NOT authentication. */
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = 0xffffffffU;
    unsigned bit;
    while (n--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xedb88320U);
    }
    return ~crc;
}
static bool valid(const Exp3Message *m) {
    return m && m->controller && m->hmi && m->request && m->action && m->generation && m->target &&
           m->kind >= EXP3_CONFIRM && m->kind <= EXP3_RESULT &&
           ((m->kind == EXP3_RESULT && m->sender == 2 && m->status >= EXP3_ACCEPTED &&
             m->status <= EXP3_SESSION && m->command >= EXP3_CONFIRM && m->command <= EXP3_QUERY) ||
            (m->kind != EXP3_RESULT && m->sender == 1 && !m->status && !m->command));
}
bool exp3_encode(const Exp3Message *m, uint8_t out[EXP3_MESSAGE_BYTES]) {
    if (!out || !valid(m))
        return false;
    out[0] = 0xe3;
    out[1] = EXP3_WIRE_VERSION;
    out[2] = m->kind;
    out[3] = m->sender;
    put32(out + 4, m->controller);
    put32(out + 8, m->hmi);
    put32(out + 12, m->request);
    put32(out + 16, m->action);
    put32(out + 20, m->context);
    put32(out + 24, m->generation);
    out[28] = (uint8_t)m->target;
    out[29] = (uint8_t)(m->target >> 8);
    out[30] = m->status;
    out[31] = m->command;
    put32(out + 32, crc32(out, 32));
    return true;
}
bool exp3_decode(const uint8_t *b, size_t n, Exp3Message *m) {
    Exp3Message v;
    if (!b || !m || n != EXP3_MESSAGE_BYTES || b[0] != 0xe3 || b[1] != EXP3_WIRE_VERSION ||
        get32(b + 32) != crc32(b, 32))
        return false;
    memset(&v, 0, sizeof v);
    v.kind = b[2];
    v.sender = b[3];
    v.controller = get32(b + 4);
    v.hmi = get32(b + 8);
    v.request = get32(b + 12);
    v.action = get32(b + 16);
    v.context = get32(b + 20);
    v.generation = get32(b + 24);
    v.target = (uint16_t)(b[28] | ((uint16_t)b[29] << 8));
    v.status = b[30];
    v.command = b[31];
    if (!valid(&v))
        return false;
    *m = v;
    return true;
}

#if MOWER_BLADE_OK_ENABLED
bool exp3_completion_encode(const Exp3Message *m, uint8_t bytes[EXP3_COMPLETION_BYTES]) {
    uint8_t full[EXP3_MESSAGE_BYTES];
    if (!bytes || !m || m->kind != EXP3_RESULT || m->request != m->action ||
        m->action > 65535U || m->generation > 65535U || m->target != 1U ||
        (m->context != EXP3_PWM_CONTEXT && m->context != EXP3_UNLOCK_CONTEXT) ||
        !exp3_encode(m, full)) return false;
    memcpy(bytes, full + 16, 2U);
    memcpy(bytes + 2, full + 24, 2U);
    /* command[1:0], status[4:2], unlock context[5], reserved[7:6]=0. */
    bytes[4] = (uint8_t)(m->command | (m->status << 2) |
                         (m->context == EXP3_UNLOCK_CONTEXT ? 0x20U : 0U));
    memcpy(bytes + 5, full + 32, 4U);
    return true;
}

bool exp3_completion_decode(const uint8_t bytes[EXP3_COMPLETION_BYTES], uint32_t controller,
                            uint32_t hmi, Exp3Message *m) {
    uint8_t full[EXP3_MESSAGE_BYTES] = {0};
    if (!bytes || !m || (bytes[4] & 0xC0U)) return false;
    full[0] = 0xE3U; full[1] = EXP3_WIRE_VERSION;
    full[2] = EXP3_RESULT; full[3] = 2U;
    put32(full + 4, controller); put32(full + 8, hmi);
    memcpy(full + 12, bytes, 2U); memcpy(full + 16, bytes, 2U);
    put32(full + 20, bytes[4] & 0x20U ? EXP3_UNLOCK_CONTEXT : EXP3_PWM_CONTEXT);
    memcpy(full + 24, bytes + 2, 2U); full[28] = 1U;
    full[30] = (bytes[4] >> 2) & 7U; full[31] = bytes[4] & 3U;
    memcpy(full + 32, bytes + 5, 4U);
    return exp3_decode(full, sizeof(full), m);
}

uint32_t exp3_completion_part(const uint8_t bytes[EXP3_COMPLETION_BYTES], uint8_t sequence,
                              unsigned part) {
    if (!bytes || !sequence || part >= EXP3_COMPLETION_PARTS) return 0U;
    return (uint32_t)sequence | ((uint32_t)bytes[part*3U] << 8) |
        ((uint32_t)bytes[part*3U+1U] << 16) | ((uint32_t)bytes[part*3U+2U] << 24);
}

int exp3_completion_receive(Exp3CompletionRx *rx, unsigned part, uint32_t word,
                            uint32_t now, uint32_t controller, uint32_t hmi,
                            Exp3Message *m) {
    uint8_t sequence = (uint8_t)word, delta;
    if (!rx || !m || !sequence || part >= EXP3_COMPLETION_PARTS) return EXP3_REJECT;
    if (!rx->sequence || (uint32_t)(now-rx->started) >= EXP3_COMPLETION_FRESHNESS_MS) {
        rx->sequence = sequence; rx->mask = 0U; rx->started = now;
    } else if (rx->sequence != sequence) {
        delta = (uint8_t)(sequence-rx->sequence);
        if (delta >= 128U) return EXP3_REJECT;
        rx->sequence = sequence; rx->mask = 0U; rx->started = now;
    }
    if (rx->mask & (1U << part)) return EXP3_DUPLICATE;
    rx->bytes[part*3U] = (uint8_t)(word >> 8);
    rx->bytes[part*3U+1U] = (uint8_t)(word >> 16);
    rx->bytes[part*3U+2U] = (uint8_t)(word >> 24);
    rx->mask |= (uint8_t)(1U << part);
    if (rx->mask != 7U) return EXP3_MORE;
    return exp3_completion_decode(rx->bytes, controller, hmi, m) ? EXP3_COMPLETE : EXP3_REJECT;
}
#endif

bool exp3_fragment(const uint8_t b[EXP3_MESSAGE_BYTES], uint32_t transfer, unsigned index,
                   uint8_t f[EXP3_FRAGMENT_BYTES]) {
    unsigned offset = index * 8U, count;
    if (!b || !f || !transfer || index >= EXP3_FRAGMENT_COUNT)
        return false;
    memset(f, 0, EXP3_FRAGMENT_BYTES);
    put32(f, transfer);
    f[4] = (uint8_t)index;
    f[5] = EXP3_FRAGMENT_COUNT;
    count = EXP3_MESSAGE_BYTES - offset;
    if (count > 8)
        count = 8;
    memcpy(f + 6, b + offset, count);
    put32(f + 14, crc32(f, 14));
    return true;
}
bool exp3_expire(Exp3Rx *r, uint32_t now, uint32_t timeout) {
    if (r->active && (!timeout || (uint32_t)(now - r->started) >= timeout)) {
        r->active = 0;
        return true;
    }
    return false;
}
int exp3_receive(Exp3Rx *r, const uint8_t *f, size_t n, uint32_t now, uint32_t timeout,
                 Exp3Message *out) {
    uint32_t transfer;
    unsigned index;
    if (!r || !f || !out)
        return EXP3_REJECT;
    (void)exp3_expire(r, now, timeout);
    if (n != EXP3_FRAGMENT_BYTES || !timeout || get32(f + 14) != crc32(f, 14) ||
        f[5] != EXP3_FRAGMENT_COUNT || f[4] >= EXP3_FRAGMENT_COUNT) {
        r->active = 0;
        return EXP3_REJECT;
    }
    transfer = get32(f);
    index = f[4];
    if (!transfer || transfer < r->transfer)
        return EXP3_REJECT;
    if (transfer > r->transfer) {
        r->transfer = transfer;
        r->active = 0;
        r->next = 0;
        if (index != 0)
            return EXP3_REJECT;
        r->started = now;
        r->active = 1;
    }
    if (!r->active)
        return EXP3_REJECT;
    if (index < r->next) {
        if (memcmp(r->bytes + index * 8, f + 6, 8) == 0)
            return EXP3_DUPLICATE;
        r->active = 0;
        return EXP3_REJECT;
    }
    if (index != r->next) {
        r->active = 0;
        return EXP3_REJECT;
    }
    memcpy(r->bytes + index * 8, f + 6, 8);
    ++r->next;
    if (r->next < EXP3_FRAGMENT_COUNT)
        return EXP3_MORE;
    r->active = 0;
    if (r->bytes[36] || r->bytes[37] || r->bytes[38] || r->bytes[39])
        return EXP3_REJECT;
    return exp3_decode(r->bytes, EXP3_MESSAGE_BYTES, out) ? EXP3_COMPLETE : EXP3_REJECT;
}

void exp3_bind(Exp3Endpoint *e, uint32_t controller, uint32_t hmi) {
    memset(e, 0, sizeof *e);
    e->controller = controller;
    e->hmi = hmi;
}
static bool identity_equal(const Exp3Message *a, const Exp3Message *b) {
    return a->controller == b->controller && a->hmi == b->hmi && a->request == b->request &&
           a->action == b->action && a->target == b->target && a->context == b->context &&
           a->generation == b->generation;
}
bool exp3_decide(Exp3Endpoint *e, const Exp3Message *m, Exp3Message *result) {
    unsigned i;
    uint8_t status = EXP3_UNKNOWN;
    Exp3Message *record = NULL;
    if (!e || !result || !valid(m) || m->kind == EXP3_RESULT)
        return false;
    if (m->controller != e->controller || m->hmi != e->hmi)
        status = EXP3_SESSION;
    else {
        for (i = 0; i < EXP3_CACHE_SIZE; i++)
            if (e->records[i].action == m->action) {
                record = &e->records[i];
                break;
            }
        if (record) {
            if (!identity_equal(record, m))
                status = EXP3_CONFLICT;
            else {
                if (m->kind == EXP3_CANCEL)
                    record->status = EXP3_CANCELLED;
                status = record->status;
            }
        } else if (m->kind == EXP3_QUERY)
            status = EXP3_UNKNOWN;
        else if (m->action <= e->high_action)
            status = EXP3_STALE;
        else {
            e->high_action = m->action;
            record = &e->records[e->next];
            e->next = (uint8_t)((e->next + 1) % EXP3_CACHE_SIZE);
            *record = *m;
            status = m->kind == EXP3_CANCEL ? EXP3_CANCELLED : EXP3_ACCEPTED;
            record->status = status;
            if (status == EXP3_ACCEPTED)
                ++e->created;
        }
    }
    *result = *m;
    result->kind = EXP3_RESULT;
    result->sender = 2;
    result->command = m->kind;
    result->status = status;
    return true;
}
#endif
