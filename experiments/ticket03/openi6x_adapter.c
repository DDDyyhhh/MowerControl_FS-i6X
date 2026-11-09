#include "exp3_board.h"
#include "bag_connection.h"
#if EXP3_ENABLED
Exp3Hil g_exp3_hil;
#if MOWER_BLADE_OK_ENABLED
bool exp3_board_defer_result(const Exp3Message *result) { (void)result; return false; }
static uint16_t control_word, control_generation;
static uint32_t control_at, generation_at;
static uint8_t control_seen, control_valid;
bool exp3_board_control(uint32_t now, uint16_t *generation, uint16_t *status) {
    if (!generation || !status || !g_exp3_hil.bound || !control_valid || !control_generation ||
        (uint32_t)(now-control_at)>=1000U || (uint32_t)(now-generation_at)>=1000U) return false;
    *generation=control_generation; *status=control_word; return true;
}
static void receive_control(uint8_t address, uint16_t value, uint32_t now) {
    if (!g_exp3_hil.bound) return;
    if (address==MOWER_UNLOCK_ADDRESS) { control_generation=value; generation_at=now; return; }
    if (!(value&MOWER_CONTROL_VALID) || ((value>>3)&3U)>2U ||
        ((value&MOWER_CONTROL_ARMED) && (value&MOWER_CONTROL_READY))) {
        control_seen=control_valid=0U; return;
    }
    if (!control_seen || (uint32_t)(now-control_at)>=1000U) {
        control_seen=1U; control_valid=0U; control_at=now; control_word=value; return;
    }
    uint16_t delta=(uint16_t)(((value>>6)-(control_word>>6))&MOWER_CONTROL_HEARTBEAT_MASK);
    if (delta && delta<512U) { control_valid=1U; control_at=now; control_word=value; }
    else if (!delta && !(value&MOWER_CONTROL_ARMED) &&
        (!(value&MOWER_CONTROL_READY) || (control_word&MOWER_CONTROL_READY))) control_word=value;
}
static uint16_t runtime_word;
static uint32_t runtime_at;
static uint8_t runtime_seen, runtime_valid;
static uint16_t bag_word;
static uint32_t bag_at;
static uint8_t bag_seen, bag_valid;
bool exp3_board_bag_connection(uint32_t now, bool *connected) {
    if (!connected || !g_exp3_hil.bound || !bag_valid ||
        (uint32_t)(now-bag_at) >= MOWER_BAG_FRESHNESS_MS) return false;
    *connected = (bag_word & MOWER_BAG_CONNECTED) != 0U;
    return true;
}
static void receive_bag_connection(uint16_t word, uint32_t now) {
    uint16_t delta;
    if (!g_exp3_hil.bound) return;
    if (!(word & MOWER_BAG_SAMPLE_VALID)) {
        bag_seen=bag_valid=0U;
        return;
    }
    if (!bag_seen || (uint32_t)(now-bag_at) >= MOWER_BAG_FRESHNESS_MS) {
        bag_word=word; bag_at=now; bag_seen=1U; bag_valid=0U;
        return;
    }
    delta=(uint16_t)(((word>>2)-(bag_word>>2)) & MOWER_BAG_HEARTBEAT_MASK);
    if (delta && delta < 8192U) {
        bag_word=word; bag_at=now; bag_valid=1U;
    } else if (!delta && !(word & MOWER_BAG_CONNECTED)) {
        /* Disconnect wins within one heartbeat; delayed connected cannot undo it. */
        bag_word=word;
    }
}
bool exp3_board_runtime(uint32_t now, uint16_t *word) {
    if (!word || !g_exp3_hil.bound || !runtime_valid ||
        (uint32_t)(now-runtime_at) >= 1000U) return false;
    *word = runtime_word;
    return true;
}
static void receive_runtime(uint16_t word, uint32_t now) {
    uint16_t delta;
    if (!g_exp3_hil.bound || !(word&8U) ||
        ((word&1U) && !(word&6U)) || (!(word&1U) && (word&6U))) return;
    if (!runtime_seen || (uint32_t)(now-runtime_at)>=1000U) {
        runtime_word=word; runtime_at=now; runtime_seen=1U; runtime_valid=0U; return;
    }
    delta=(uint16_t)(((word>>4)-(runtime_word>>4))&0xFFFU);
    if (delta && delta<2048U) {
        runtime_at=now; runtime_valid=1U; runtime_word=word;
    } else if (!delta && ((runtime_word&1U) || !(word&1U))) {
        runtime_word=word; /* Stop is visible immediately; frozen data never renews freshness. */
    }
}
#endif
volatile Exp3Mailbox g_exp3_mailbox;
volatile uint32_t g_exp3_uplink_channels;
volatile uint32_t g_exp3_capture_overflow;
#if EXP3_AUTO_SESSION_ENABLED
static Exp3AutoSession auto_session;
static uint8_t auto_started;
static volatile uint32_t auto_seed, auto_seed_at;
void exp3_board_auto_start(uint32_t seed, uint32_t now) {
    if (auto_started) return;
    auto_seed = seed ? seed : 1U;
    auto_seed_at = now;
}
static void receive_word(uint16_t word, uint32_t now) {
    if (auto_started && exp3_auto_is_word(word)) {
        if (exp3_auto_receive(&auto_session, word, now, 5000U) == 1) {
            g_exp3_mailbox.command = 0U;
#if MOWER_BLADE_OK_ENABLED
            runtime_seen=runtime_valid=0U;
            control_seen=control_valid=0U; control_generation=0U;
            bag_seen=bag_valid=0U;
#endif
            exp3_hil_bind_session(&g_exp3_hil, NULL, auto_session.peer,
                                  auto_session.self, now);
        }
    } else
        exp3_hil_receive(&g_exp3_hil, NULL, word, now);
}
#else
static void receive_word(uint16_t word, uint32_t now) {
    exp3_hil_receive(&g_exp3_hil, NULL, word, now);
}
#endif
static volatile struct {
    uint32_t tick;
    uint8_t bytes[37];
} capture[2];
static volatile uint8_t capture_write, capture_read;
void exp3_board_status(Exp3BoardStatus *status) {
    uint32_t before, after;
    if (!status) return;
    do {
        before = *(volatile uint32_t *)&g_exp3_hil.matches;
        status->last_result = g_exp3_hil.last_result;
        after = *(volatile uint32_t *)&g_exp3_hil.matches;
    } while (before != after);
    status->controller = g_exp3_hil.controller;
    status->hmi = g_exp3_hil.hmi;
    status->matches = after;
    status->bound = g_exp3_hil.bound;
    /* A queued mailbox command is busy before the RF task consumes it. */
    status->waiting = g_exp3_hil.waiting || g_exp3_mailbox.command ||
                      g_exp3_hil.link.tx_active;
    status->cancel_ready = 0U;
    status->cancel_failed = 0U;
#if MOWER_BLADE_OK_ENABLED
    status->cancel_ready = g_exp3_hil.bound && !g_exp3_mailbox.command &&
                          !g_exp3_hil.link.tx_active &&
                          (!g_exp3_hil.waiting || g_exp3_hil.pending.kind == EXP3_CONFIRM);
    status->cancel_failed = g_exp3_hil.cancel_failed;
#endif
}
bool exp3_board_submit(const Exp3Message *message) {
    if (!message || !g_exp3_hil.bound || g_exp3_mailbox.command)
        return false;
#if MOWER_BLADE_OK_ENABLED
    if (g_exp3_hil.waiting && message->kind == EXP3_CANCEL &&
        g_exp3_hil.pending.kind == EXP3_CONFIRM && !g_exp3_hil.link.tx_active) {
        uint8_t encoded[EXP3_MESSAGE_BYTES];
        if (message->sender != 1U || !exp3_encode(message, encoded)) return false;
        const Exp3Message *old = &g_exp3_hil.pending;
        if (message->controller != old->controller || message->hmi != old->hmi ||
            message->request != old->request || message->action != old->action ||
            message->generation != old->generation || message->context != old->context ||
            message->target != old->target) return false;
        g_exp3_hil.waiting = 0U; // supersede response wait, never truncate outgoing frame
    }
#endif
    if (g_exp3_hil.waiting || g_exp3_hil.link.tx_active) return false;
    g_exp3_mailbox.message = *message;
    g_exp3_mailbox.command = EXP3_SEND;
    return true;
}
/* ISR: bounded copy only. SPSC, RF callbacks must not nest. */
void exp3_board_capture_rf(const uint8_t *p, unsigned n, uint32_t now) {
    unsigned i;
    uint8_t slot = capture_write % 2;
    if (n != 37 || (uint8_t)(capture_write - capture_read) >= 2) {
        if (g_exp3_capture_overflow != UINT32_MAX)
            ++g_exp3_capture_overflow;
        return;
    }
    capture[slot].tick = now;
    for (i = 0; i < 37; i++)
        capture[slot].bytes[i] = p[i];
    ++capture_write;
}
void exp3_board_poll(uint32_t now) {
    uint16_t a, b;
#if EXP3_AUTO_SESSION_ENABLED
    if (!auto_started && auto_seed) {
        uint32_t seed = auto_seed;
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        exp3_auto_init(&auto_session, EXP3_AUTO_HMI, seed, auto_seed_at);
        auto_started = 1U;
    }
#endif
    /* At most two frames per foreground invocation; no unbounded draining. */
    unsigned budget = 2;
    while (capture_read != capture_write && budget--) {
        uint8_t p[37], slot = capture_read % 2;
        unsigned i;
        uint32_t tick = capture[slot].tick;
        for (i = 0; i < 37; i++)
            p[i] = capture[slot].bytes[i];
        ++capture_read;
        exp3_board_rf(p, 37, tick);
    }
    exp3_hil_poll(&g_exp3_hil, &g_exp3_mailbox, 0, now, 20);
    {
        uint16_t word = g_exp3_hil.published_word;
#if EXP3_AUTO_SESSION_ENABLED
        if (auto_started && !g_exp3_hil.link.tx_active)
            word = exp3_auto_word(&auto_session, now, 20U, 2000U);
        if (word) {
            exp3_channels(word, &a, &b);
            g_exp3_uplink_channels = (uint32_t)a | ((uint32_t)b << 16);
        } else
            g_exp3_uplink_channels = 0U;
#else
        if (g_exp3_hil.bound) {
            exp3_channels(word, &a, &b);
            g_exp3_uplink_channels = (uint32_t)a | ((uint32_t)b << 16);
        }
#endif
    }
}
/* Called only after RF CRC and both binding IDs have passed. */
void exp3_board_rf(const uint8_t *p, unsigned n, uint32_t now) {
    unsigned i;
    if (n < 10)
        return;
    if (p[0] == 0xaa) {
        for (i = 9; i + 4 <= n && i < 37 && p[i] != 0xff; i += 4) {
#if MOWER_BLADE_OK_ENABLED
            if ((p[i]==MOWER_UNLOCK_TYPE && p[i+1]==MOWER_UNLOCK_ADDRESS) ||
                (p[i]==MOWER_CONTROL_TYPE && p[i+1]==MOWER_CONTROL_ADDRESS))
                receive_control(p[i+1],(uint16_t)(p[i+2] | ((uint16_t)p[i+3]<<8)),now);
            if (p[i] == 0xe2 && p[i+1] == 10U)
                receive_runtime((uint16_t)(p[i+2] | ((uint16_t)p[i+3]<<8)), now);
            if (p[i] == MOWER_BAG_SENS_TYPE && p[i+1] == MOWER_BAG_SENS_ADDRESS)
                receive_bag_connection((uint16_t)(p[i+2] | ((uint16_t)p[i+3]<<8)), now);
#endif
            if (p[i] == 0xe1 && p[i + 1] == 9)
                receive_word((uint16_t)(p[i + 2] | ((uint16_t)p[i + 3] << 8)), now);
        }
    } else if (p[0] == 0xac) {
        for (i = 9; i + 3 <= n && p[i] != 0xff;) {
            unsigned size = p[i + 2];
            if (size > n - i - 3)
                break;
#if MOWER_BLADE_OK_ENABLED
            if (size==2U && ((p[i]==MOWER_UNLOCK_TYPE && p[i+1]==MOWER_UNLOCK_ADDRESS) ||
                (p[i]==MOWER_CONTROL_TYPE && p[i+1]==MOWER_CONTROL_ADDRESS)))
                receive_control(p[i+1],(uint16_t)(p[i+3] | ((uint16_t)p[i+4]<<8)),now);
            if (p[i] == 0xe2 && p[i+1] == 10U && size == 2U)
                receive_runtime((uint16_t)(p[i+3] | ((uint16_t)p[i+4]<<8)), now);
            if (p[i] == MOWER_BAG_SENS_TYPE && p[i+1] == MOWER_BAG_SENS_ADDRESS && size == 2U)
                receive_bag_connection((uint16_t)(p[i+3] | ((uint16_t)p[i+4]<<8)), now);
#endif
            if (p[i] == 0xe1 && p[i + 1] == 9 && size == 2)
                receive_word((uint16_t)(p[i + 3] | ((uint16_t)p[i + 4] << 8)), now);
            i += 3 + size;
        }
    }
}
#endif
