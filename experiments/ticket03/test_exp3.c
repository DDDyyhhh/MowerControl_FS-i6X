#include "exp3.h"
#include "exp3_hil.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Exp3Message message(void) {
    Exp3Message m = {0};
    m.kind = EXP3_CONFIRM;
    m.sender = 1;
    m.controller = 0x11223344;
    m.hmi = 0x55667788;
    m.request = 1;
    m.action = 1;
    m.context = 9;
    m.generation = 3;
    m.target = 2;
    return m;
}
static void fragments(void) {
    Exp3Rx rx = {0};
    Exp3Message m = message(), out;
    uint8_t b[EXP3_MESSAGE_BYTES], f[EXP3_FRAGMENT_BYTES];
    unsigned i;
    assert(exp3_encode(&m, b));
    for (i = 0; i < EXP3_FRAGMENT_COUNT; i++) {
        assert(exp3_fragment(b, 1, i, f));
        assert(exp3_receive(&rx, f, sizeof f, 100 + i, 1000, &out) ==
               (i == EXP3_FRAGMENT_COUNT - 1 ? EXP3_COMPLETE : EXP3_MORE));
    }
    assert(out.hmi == m.hmi && out.context == 9);
    assert(exp3_receive(&rx, f, sizeof f, 200, 1000, &out) == EXP3_REJECT);
    puts("PASS fragment complete and replay rejection");
}
static void endpoint(void) {
    Exp3Endpoint e;
    Exp3Message m = message(), result;
    exp3_bind(&e, m.controller, m.hmi);
    m.kind = EXP3_QUERY;
    assert(exp3_decide(&e, &m, &result) && result.status == EXP3_UNKNOWN);
    m.kind = EXP3_CONFIRM;
    assert(exp3_decide(&e, &m, &result) && result.status == EXP3_ACCEPTED);
    assert(exp3_decide(&e, &m, &result) && result.status == EXP3_ACCEPTED);
    m.kind = EXP3_CANCEL;
    assert(exp3_decide(&e, &m, &result) && result.status == EXP3_CANCELLED);
    m.kind = EXP3_CONFIRM;
    assert(exp3_decide(&e, &m, &result) && result.status == EXP3_CANCELLED);
    puts("PASS query/confirm/duplicate/cancel");
}
static void faults(void) {
    Exp3Rx rx = {0};
    Exp3Message m = message(), out;
    uint8_t b[36], other[36], f[18];
    unsigned i;
    const uint8_t golden[36] = {0xe3, 0x31, 1, 1, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
                                1,    0,    0, 0, 1,    0,    0,    0,    9,    0,    0,    0,
                                3,    0,    0, 0, 2,    0,    0,    0,    0x1a, 0x8e, 0xaf, 0x0f};
    exp3_encode(&m, b);
    assert(memcmp(b, golden, 36) == 0);
    assert(!exp3_decode(b, 35, &out));
    b[1] = 1;
    assert(!exp3_decode(b, 36, &out));
    exp3_encode(&m, b);
    exp3_fragment(b, 1, 0, f);
    assert(exp3_receive(&rx, f, 18, 0, 100, &out) == EXP3_MORE);
    assert(exp3_receive(&rx, f, 18, 99, 100, &out) == EXP3_DUPLICATE);
    assert(exp3_receive(&rx, f, 18, 100, 100, &out) == EXP3_REJECT);
    exp3_fragment(b, 2, 0, f);
    assert(exp3_receive(&rx, f, 18, 200, 100, &out) == EXP3_MORE);
    exp3_fragment(b, 2, 2, f);
    assert(exp3_receive(&rx, f, 18, 201, 100, &out) == EXP3_REJECT);
    exp3_fragment(b, 2, 1, f);
    assert(exp3_receive(&rx, f, 18, 202, 100, &out) == EXP3_REJECT);
    exp3_fragment(b, 3, 1, f);
    assert(exp3_receive(&rx, f, 18, 203, 100, &out) == EXP3_REJECT);
    exp3_fragment(b, 4, 0, f);
    assert(exp3_receive(&rx, f, 18, 204, 100, &out) == EXP3_MORE);
    memcpy(other, b, 36);
    other[4] ^= 1;
    exp3_fragment(other, 4, 0, f);
    assert(exp3_receive(&rx, f, 18, 205, 100, &out) == EXP3_REJECT);
    /* Each fragment CRC is valid, but a mixed message must fail end-to-end CRC. */
    m.hmi++;
    exp3_encode(&m, other);
    for (i = 0; i < 5; i++) {
        exp3_fragment(i == 1 ? other : b, 5, i, f);
        assert(exp3_receive(&rx, f, 18, 210 + i, 100, &out) == (i == 4 ? EXP3_REJECT : EXP3_MORE));
    }
    exp3_fragment(b, 6, 0, f);
    assert(exp3_receive(&rx, f, 18, 0xfffffff0U, 32, &out) == EXP3_MORE);
    assert(exp3_expire(&rx, 0x10, 32));
    exp3_fragment(b, 6, 1, f);
    assert(exp3_receive(&rx, f, 18, 0x11, 32, &out) == EXP3_REJECT);
    puts("PASS golden/truncation/version/missing/order/duplicate/conflict/mixed/timeout/wrap");
}
static void endpoint_faults(void) {
    Exp3Endpoint e;
    Exp3Message m = message(), out;
    unsigned i;
    exp3_bind(&e, m.controller, m.hmi);
    m.kind = EXP3_CANCEL;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_CANCELLED);
    m.kind = EXP3_CONFIRM;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_CANCELLED);
    for (i = 2; i <= 6; i++) {
        m.action = i;
        m.request = i;
        assert(exp3_decide(&e, &m, &out) && out.status == EXP3_ACCEPTED);
    }
    m.action = 1;
    m.request = 1;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_STALE);
    m.kind = EXP3_QUERY;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_UNKNOWN);
    m.action = 6;
    m.request = 6;
    m.context++;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_CONFLICT);
    m.kind = EXP3_CONFIRM;
    m.action = 7;
    m.request = 7;
    m.hmi++;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_SESSION);
    exp3_bind(&e, m.controller + 1, m.hmi);
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_SESSION);
    m.controller++;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_ACCEPTED);
    m.action = 0xffffffffU;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_ACCEPTED);
    m.action = 1;
    assert(exp3_decide(&e, &m, &out) && out.status == EXP3_STALE);
    puts("PASS cancel-before-confirm/eviction/conflict/reboot/session/action exhaustion");
}
static void transport(void) {
    Exp3Link tx, rx;
    Exp3Message m = message(), out;
    uint16_t a, b, w;
    unsigned i;
    int n = 0;
    exp3_link_init(&tx);
    exp3_link_init(&rx);
    assert(exp3_link_send(&tx, &m, 0));
    for (i = 0; i < 90; i++) {
        w = exp3_link_word(&tx, i * 20, 20);
        exp3_channels(w, &a, &b);
        assert(exp3_channel_word(a, b, &w));
        if (exp3_link_receive(&rx, w, i * 20, 60000, &out) == EXP3_COMPLETE)
            n++;
        assert(exp3_link_receive(&rx, w, i * 20 + 1, 60000, &out) != EXP3_COMPLETE);
    }
    assert(n == 1 && out.action == 1);
    assert(!exp3_channel_word(1500, 1500, &w));
    for (i = 0; i < 100; i++)
        assert(exp3_link_receive(&rx, 0, 2000 + i, 60000, &out) != EXP3_COMPLETE);
    puts("PASS transport complete/static/duplicate/failsafe");
}
static void hil(void) {
    Exp3Hil h = {0}, c = {0};
    Exp3Endpoint ep;
    volatile Exp3Mailbox hb = {0}, cb = {0};
    unsigned t;
    hb.message = message();
    cb.message = message();
    hb.command = EXP3_ARM;
    cb.command = EXP3_ARM;
    exp3_hil_poll(&h, &hb, NULL, 0, 20);
    exp3_hil_poll(&c, &cb, &ep, 0, 250);
    hb.command = EXP3_SEND;
    for (t = 1; t < 30000; t++) {
        exp3_hil_poll(&h, &hb, NULL, t, 20);
        exp3_hil_poll(&c, &cb, &ep, t, 250);
        if (t % 4 == 0)
            exp3_hil_receive(&c, &ep, h.published_word, t);
        if (t % 120 == 0)
            exp3_hil_receive(&h, NULL, c.published_word, t);
    }
    assert(h.matches == 1 && h.last_result.status == EXP3_ACCEPTED);
    assert(h.rtt_ms > 20000 && h.rtt_ms < 30000 && h.log_overflow > 0);
    hb.command = EXP3_ARM;
    hb.message.hmi++;
    exp3_hil_poll(&h, &hb, NULL, 30001, 20);
    assert(h.hmi == message().hmi);
    puts("PASS HIL same-clock RTT, bounded overflow, one-time session binding");
}
static void pair_run(Exp3Hil *h, Exp3Hil *c, Exp3Mailbox *hb, Exp3Mailbox *cb, Exp3Endpoint *ep,
                     unsigned start, unsigned end, int uplink, int downlink) {
    unsigned t;
    for (t = start; t < end; t += 4) {
        exp3_hil_poll(h, hb, NULL, t, 20);
        exp3_hil_poll(c, cb, ep, t, 250);
        if (uplink)
            exp3_hil_receive(c, ep, h->published_word, t);
        if (downlink && t % 120 == 0)
            exp3_hil_receive(h, NULL, c->published_word, t);
        /* Live collector consumes all published entries without touching protocol state. */
        h->log_read = h->log_write;
        c->log_read = c->log_write;
    }
}
static void link_faults(void) {
    Exp3Hil h = {0}, c = {0};
    Exp3Endpoint ep;
    Exp3Mailbox hb = {0}, cb = {0};
    hb.message = message();
    cb.message = message();
    hb.command = EXP3_ARM;
    cb.command = EXP3_ARM;
    pair_run(&h, &c, &hb, &cb, &ep, 0, 4, 1, 1);
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 4, 61004, 0, 1);
    assert(h.matches == 0 && !h.waiting && ep.created == 0); /* uplink lost */
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 61004, 122004, 1, 0);
    assert(h.matches == 0 && !h.waiting && ep.created == 1); /* result lost */
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 122004, 152004, 1, 1);
    assert(h.matches == 1 && ep.created == 1); /* retry is idempotent */
    /* A fresh action interrupted by actual RF failsafe cannot finish. */
    hb.message.action = 2;
    hb.message.request = 2;
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 152004, 152104, 1, 1);
    exp3_hil_channels(&c, &ep, 1500, 1500, 152104);
    pair_run(&h, &c, &hb, &cb, &ep, 152108, 213108, 1, 1);
    assert(ep.created == 1 && h.matches == 1);
    /* One receiver reboot is unbound even if old peer keeps sending. */
    memset(&c, 0, sizeof c);
    memset(&ep, 0, sizeof ep);
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 213108, 274108, 1, 1);
    assert(ep.created == 0 && !c.bound && h.matches == 1);
    /* Both reboot and provision fresh pair; replay from old sessions is rejected. */
    memset(&h, 0, sizeof h);
    memset(&c, 0, sizeof c);
    hb.message.controller++;
    hb.message.hmi++;
    cb.message = hb.message;
    hb.command = EXP3_ARM;
    cb.command = EXP3_ARM;
    pair_run(&h, &c, &hb, &cb, &ep, 274108, 274112, 1, 1);
    hb.command = EXP3_SEND;
    pair_run(&h, &c, &hb, &cb, &ep, 274112, 304112, 1, 1);
    assert(h.matches == 1 && ep.created == 1);
    {
        Exp3Message old = message(), result;
        assert(exp3_decide(&ep, &old, &result) && result.status == EXP3_SESSION);
    }
    assert(h.log_overflow == 0 && c.log_overflow == 0);
    puts("PASS one-way loss/retry/failsafe/reboot/fresh-session recovery/live log drain");
}
int main(void) {
    assert(sizeof(Exp3Mailbox) == 36 && offsetof(Exp3Mailbox, command) == 32);
    assert(offsetof(Exp3Hil, log_write) == 228 && offsetof(Exp3Hil, events) == 240);
    assert(sizeof(Exp3Hil) == 244 + 16 * EXP3_LOG_COUNT);
    link_faults();
    hil();
    transport();
    faults();
    endpoint_faults();
    endpoint();
    fragments();
    Exp3Message m = message(), decoded;
    uint8_t bytes[EXP3_MESSAGE_BYTES];
    assert(exp3_encode(&m, bytes));
    assert(bytes[0] == 0xE3 && bytes[1] == 0x31 && bytes[2] == EXP3_CONFIRM);
    assert(bytes[4] == 0x44 && bytes[7] == 0x11);
    assert(exp3_decode(bytes, sizeof bytes, &decoded));
    assert(decoded.controller == m.controller && decoded.action == 1);
    bytes[12] ^= 1;
    assert(!exp3_decode(bytes, sizeof bytes, &decoded));
    puts("PASS codec identity and corruption");
    return 0;
}
