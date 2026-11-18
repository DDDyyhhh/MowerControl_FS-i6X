#include "exp3_link.h"
#include "exp3_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Exp3Message request(unsigned kind, unsigned context)
{
    Exp3Message m = {0};
    m.controller = 0x12345678U; m.hmi = 0xabcdef01U;
    m.request = m.action = 321U; m.generation = 654U;
    m.context = context; m.target = EXP3_PWM_TARGET;
    m.kind = kind; m.sender = 1U;
    return m;
}

static unsigned transmit(const Exp3Message *m, uint16_t words[90])
{
    Exp3Link tx = {0};
    unsigned n;
    assert(exp3_link_send(&tx, m, 0U));
    for (n = 0; n < 90U; ++n) {
        words[n] = exp3_link_word(&tx, n * 20U, 20U);
        if (!words[n]) break;
    }
    return n;
}

static uint32_t receiver_hmi=0xabcdef01U;
static int receive(const uint16_t *words, unsigned n, Exp3Message *out)
{
    Exp3Link rx = {.controller=0x12345678U,.hmi=0};
    rx.hmi=receiver_hmi;
    int rc = EXP3_MORE;
    for (unsigned i = 0; i < n; ++i) {
        uint16_t a, b, word;
        exp3_channels(words[i], &a, &b);
        assert(exp3_channel_word(a, b, &word) && word == words[i]);
        rc = exp3_link_receive(&rx, word, i * 20U, 60000U, out);
        assert(exp3_link_receive(&rx, word, i * 20U + 1U, 60000U, out) == EXP3_DUPLICATE);
    }
    return rc;
}

int main(void)
{
    for (unsigned context = 0; context < 2U; ++context) {
        for (unsigned kind = EXP3_CONFIRM; kind <= EXP3_QUERY; ++kind) {
            Exp3Message m = request(kind, context ? EXP3_UNLOCK_CONTEXT : EXP3_PWM_CONTEXT), out;
            uint16_t words[90], bad[90];
            uint8_t expected[36], actual[36];
            unsigned n = transmit(&m, words);
            if (n != 9U) { fprintf(stderr, "FAIL compact command: words=%u expected=9\n", n); return 1; }
            assert(receive(words, n, &out) == EXP3_COMPLETE);
            receiver_hmi=0U; assert(receive(words,n,&out)!=EXP3_COMPLETE);
            receiver_hmi=0xabcdef02U; assert(receive(words,n,&out)!=EXP3_COMPLETE);
            receiver_hmi=0xabcdef01U; assert(receive(words,n,&out)==EXP3_COMPLETE);
            assert(exp3_encode(&m, expected) && exp3_encode(&out, actual));
            assert(!memcmp(expected, actual, sizeof(expected)));
            for (unsigned i = 0; i < n; ++i) {
                memcpy(bad, words, sizeof(words)); bad[i] ^= 1U;
                assert(receive(bad, n, &out) != EXP3_COMPLETE);
                memcpy(bad, words, sizeof(words));
                memmove(bad+i, bad+i+1, (n-i-1U)*sizeof(*bad));
                assert(receive(bad, n-1U, &out) != EXP3_COMPLETE);
            }
            memcpy(bad, words, sizeof(words)); bad[3] = words[4];
            assert(receive(bad, n, &out) != EXP3_COMPLETE);
            m.action = m.request = 65536U;
            assert(transmit(&m, words) == 90U && receive(words, 90U, &out) == EXP3_COMPLETE);
            assert(out.action == 65536U);
            m.action = m.request = 321U; m.generation = 65536U;
            assert(transmit(&m, words) == 90U && receive(words, 90U, &out) == EXP3_COMPLETE);
            assert(out.generation == 65536U);
        }
    }
    /* A completed transport message is still subject to endpoint replay/session checks. */
    Exp3Endpoint endpoint; Exp3Message m = request(EXP3_CONFIRM, EXP3_UNLOCK_CONTEXT), out, reply;
    uint16_t words[90]; unsigned n = transmit(&m, words);
    exp3_bind(&endpoint, m.controller, m.hmi);
    assert(receive(words, n, &out) == EXP3_COMPLETE && exp3_decide(&endpoint, &out, &reply));
    assert(exp3_decide(&endpoint, &out, &reply)); /* cached reply only */
    out.hmi++; assert(exp3_decide(&endpoint, &out, &reply) && reply.status == EXP3_SESSION);
    printf("PASS compact commands: 9 words/180ms, full identity/CRC32, loss/corruption/order/replay and wide fallback\n");
}
