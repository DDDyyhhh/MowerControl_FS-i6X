#include "exp3_probe.h"
#include "exp3_hil.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Exp3Message result(void)
{
    Exp3Message m = {0};
    m.controller = 0x11223344U;
    m.hmi = 0x55667788U;
    m.request = m.action = 321U;
    m.generation = 654U;
    m.context = EXP3_PWM_CONTEXT;
    m.target = EXP3_PWM_TARGET;
    m.kind = EXP3_RESULT;
    m.sender = 2U;
    m.command = EXP3_CONFIRM;
    m.status = EXP3_ACCEPTED;
    return m;
}

static unsigned words(const Exp3Message *m, uint16_t out[90])
{
    Exp3Link tx = {0};
    unsigned n;
    assert(exp3_link_send_result(&tx, m, 0U));
    for (n = 0U; n < 90U; ++n) {
        out[n] = exp3_link_word(&tx, n * 250U, 250U);
        if (!out[n])
            break;
    }
    return n;
}

static int receive(const uint16_t *w, unsigned n, Exp3Message *out)
{
    Exp3Link rx = {0};
    unsigned i;
    int rc = EXP3_MORE;
    for (i = 0U; i < n; ++i)
        rc = exp3_link_receive(&rx, w[i], i * 250U, 60000U, out);
    return rc;
}

static void lossless_results_and_corrupt_frames(void)
{
    Exp3Message m = result(), decoded;
    uint16_t w[90], other[90], damaged[90];
    uint8_t expected[36], actual[36];
    /* Independently generated with Python struct.pack and zlib.crc32. */
    const uint16_t golden[17] = {
        0xe044U, 0xe133U, 0xe222U, 0xe311U, 0xe488U, 0xe577U,
        0xe666U, 0xe755U, 0xe841U, 0xe901U, 0xea8eU, 0xeb02U,
        0xec11U, 0xed87U, 0xee14U, 0xef3bU, 0xf0fcU
    };
    unsigned n, i;
    Exp3Link rx = {0};
    n = words(&m, w);
    assert(n == 17U);
    assert(!memcmp(w, golden, sizeof(golden)));
    assert(receive(w, n, &decoded) == EXP3_COMPLETE);
    assert(exp3_encode(&m, expected) && exp3_encode(&decoded, actual));
    assert(!memcmp(expected, actual, sizeof(expected)));
    for (i = 0U; i < n; ++i) {
        assert(exp3_link_receive(&rx, w[i], i * 250U, 60000U, &decoded) ==
               (i == n - 1U ? EXP3_COMPLETE : EXP3_MORE));
        assert(exp3_link_receive(&rx, w[i], i * 250U + 120U, 60000U, &decoded) == EXP3_DUPLICATE);
    }
    memcpy(damaged, w, sizeof(w));
    damaged[3] ^= 1U;
    assert(receive(damaged, n, &decoded) != EXP3_COMPLETE);
    memcpy(damaged, w, sizeof(w));
    damaged[4] = w[5];
    assert(receive(damaged, n, &decoded) != EXP3_COMPLETE);
    assert(receive(w, n - 1U, &decoded) != EXP3_COMPLETE);
    m.hmi++;
    assert(words(&m, other) == n);
    memcpy(damaged, w, sizeof(w));
    memcpy(damaged + 8, other + 8, (n - 8U) * sizeof(*w));
    assert(receive(damaged, n, &decoded) != EXP3_COMPLETE);
    exp3_link_init(&rx);
    assert(exp3_link_receive(&rx, w[0], 0U, 60000U, &decoded) == EXP3_MORE);
    assert(exp3_link_receive(&rx, w[1], 60000U, 60000U, &decoded) == EXP3_REJECT);
    m = result();
    m.command = EXP3_CANCEL;
    m.status = EXP3_CANCELLED;
    assert(words(&m, w) == n && receive(w, n, &decoded) == EXP3_COMPLETE);
    assert(decoded.command == EXP3_CANCEL && decoded.status == EXP3_CANCELLED);
    m.action = m.request = 65536U; /* Full-width identity never gets truncated. */
    n = words(&m, w);
    assert(n == 90U && receive(w, n, &decoded) == EXP3_COMPLETE);
    assert(decoded.action == 65536U);
}

static void stale_result_cannot_complete_current_wait(void)
{
    Exp3Hil hmi = {0};
    Exp3Mailbox box = {0};
    Exp3Message m = result();
    uint16_t w[90];
    unsigned n, i, scenario;
    exp3_hil_bind_session(&hmi, NULL, m.controller, m.hmi, 0U);
    box.message = m;
    box.message.kind = EXP3_CONFIRM;
    box.message.sender = 1U;
    box.message.status = box.message.command = 0U;
    box.command = EXP3_SEND;
    exp3_hil_poll(&hmi, &box, NULL, 1U, 20U);
    assert(hmi.waiting);
    for (scenario = 0U; scenario < 4U; ++scenario) {
        Exp3Message old = m;
        if (scenario == 0U) old.hmi++;
        if (scenario == 1U) old.generation++;
        if (scenario == 2U) old.action = ++old.request;
        if (scenario == 3U) { old.command = EXP3_CANCEL; old.status = EXP3_CANCELLED; }
        n = words(&old, w);
        for (i = 0U; i < n; ++i)
            exp3_hil_receive(&hmi, NULL, w[i], scenario * 5000U + i * 250U + 10U);
        assert(hmi.waiting && hmi.matches == 0U);
    }
    n = words(&m, w);
    for (i = 0U; i < n; ++i)
        exp3_hil_receive(&hmi, NULL, w[i], 20010U + i * 250U);
    assert(!hmi.waiting && hmi.matches == 1U);
}

int main(void)
{
    lossless_results_and_corrupt_frames();
    stale_result_cannot_complete_current_wait();
    puts("PASS fast result full CRC/identity, duplicate/loss/corruption/timeout and wide fallback");
}
