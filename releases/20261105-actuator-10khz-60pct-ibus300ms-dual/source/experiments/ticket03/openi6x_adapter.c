#include "exp3_board.h"
#include "bag_connection.h"
#include "height_feedback.h"
#include "battery_status.h"
#if EXP3_ENABLED
Exp3Hil g_exp3_hil;
#if MOWER_BLADE_OK_ENABLED
bool exp3_board_defer_result(const Exp3Message *result) { (void)result; return false; }
static uint16_t authority_word;
static struct {
    uint32_t minimum_at, received_at;
    uint16_t minimum, reasons, pending_minimum;
    uint8_t sequence, pending, seen, valid;
} battery;
bool exp3_board_battery(uint32_t now, uint16_t *minimum_mv, uint16_t *reasons) {
    if (!minimum_mv || !reasons || !g_exp3_hil.bound || !battery.valid ||
        (uint32_t)(now-battery.received_at) >= MOWER_BATTERY_DIAG_FRESH_MS) return false;
    *minimum_mv = battery.minimum;
    *reasons = battery.reasons;
    return true;
}
static void receive_battery(uint16_t word, uint32_t now) {
    if (!g_exp3_hil.bound) return;
    if ((word & MOWER_BATTERY_DIAG_KIND) == MOWER_BATTERY_DIAG_MIN) {
        uint16_t minimum = word & 0x1FFFU;
        if (minimum > 5000U) { battery.pending=0U; return; }
        /* Do not expose a half-received pair to the GUI. */
        battery.minimum_at = now;
        battery.pending = 1U;
        battery.pending_minimum = minimum;
    } else {
        uint8_t sequence = (uint8_t)((word >> 8U) & 31U);
        uint8_t delta = (uint8_t)((sequence-battery.sequence) & 31U);
        if (!battery.pending || (uint32_t)(now-battery.minimum_at) >= 1000U) return;
        if (!battery.seen || (uint32_t)(now-battery.received_at) >= MOWER_BATTERY_DIAG_FRESH_MS)
            battery.valid = 0U; /* First pair establishes a heartbeat baseline. */
        else {
            if (!delta || delta >= 16U) return;
            battery.valid = 1U;
        }
        battery.sequence = sequence;
        battery.minimum = battery.pending_minimum;
        battery.reasons = word & 0xFFU;
        battery.received_at = now;
        battery.pending = 0U;
        battery.seen = 1U;
    }
}
static Exp3CompletionRx completion_rx;
static void receive_completion(unsigned part, uint32_t word, uint32_t now) {
    Exp3Message result;
    /* A receipt cannot cut the outgoing command frame short. Repeated sensor
     * samples will complete the result after the last word has left the link. */
    if (!g_exp3_hil.bound || !g_exp3_hil.waiting || g_exp3_mailbox.command ||
        g_exp3_hil.link.tx_active) return;
    if (exp3_completion_receive(&completion_rx, part, word, now,
        g_exp3_hil.controller, g_exp3_hil.hmi, &result) == EXP3_COMPLETE)
        (void)exp3_hil_apply_result(&g_exp3_hil, &result, now);
}
/* Each sensor retains its independent producer clock and validity.
 * Share only the replay/freshness algorithm to fit the 128KiB radio Flash. */
#if MOWER_HEIGHT_FRESHNESS_MS != MOWER_BAG_FRESHNESS_MS || MOWER_FAST_FRESHNESS_MS != MOWER_BAG_FRESHNESS_MS
#error "Shared sample cache requires equal sensor freshness"
#endif
typedef struct {
    uint32_t at;
    uint16_t value, heartbeat;
    uint8_t state; /* 0=empty, 1=baseline, 2=progress confirmed */
} SensorSample;
static SensorSample bag, bag_full, height, runtime;
static bool sample_fresh(const SensorSample *sample, uint32_t now) {
    return g_exp3_hil.bound && sample->state == 2U &&
        (uint32_t)(now-sample->at) < MOWER_BAG_FRESHNESS_MS;
}
/* 1 accepts a new baseline/progress; 0 repeats; -1 rejects reverse order. */
static __attribute__((noinline)) int sample_observe(SensorSample *sample, uint32_t now,
                                                   uint16_t heartbeat, uint16_t mask) {
    if (!sample->state || (uint32_t)(now-sample->at) >= MOWER_BAG_FRESHNESS_MS)
        sample->state=1U;
    else {
        uint16_t delta=(uint16_t)((heartbeat-sample->heartbeat) & mask);
        if (!delta) return 0;
        if (delta > (mask>>1)) return -1;
        sample->state=2U;
    }
    sample->heartbeat=heartbeat; sample->at=now;
    return 1;
}
bool exp3_board_height(uint32_t now, uint16_t *mm) {
    if (!mm || !sample_fresh(&height, now)) return false;
    *mm=height.value & MOWER_HEIGHT_VALUE_MASK;
    return true;
}
static void receive_height(uint16_t word, uint32_t now) {
    if (!g_exp3_hil.bound) return;
    if (!mower_height_word_valid(word)) height.state=0U;
    else if (sample_observe(&height, now, word>>8, MOWER_HEIGHT_HEARTBEAT_MASK) > 0)
        height.value=word;
}
bool exp3_board_bag_connection(uint32_t now, bool *connected) {
    if (!connected || !sample_fresh(&bag, now)) return false;
    *connected=(bag.value & MOWER_BAG_CONNECTED) != 0U;
    return true;
}
bool exp3_board_bag_full(uint32_t now, bool *full) {
    bool connected;
    if (!full || !exp3_board_bag_connection(now, &connected) || !connected ||
        !sample_fresh(&bag_full, now)) return false;
    *full=(bag_full.value & MOWER_BAG_FULL) != 0U;
    return true;
}
static void receive_bag_connection(uint16_t word, uint32_t now) {
    if (!g_exp3_hil.bound) return;
    if (!(word & MOWER_BAG_SAMPLE_VALID)) {
        bag.state=bag_full.state=0U;
        return;
    }
    int step=sample_observe(&bag, now, word>>2, MOWER_BAG_HEARTBEAT_MASK);
    if (step > 0 || (!step && !(word & MOWER_BAG_CONNECTED)))
        bag.value=word; /* Disconnect wins within a heartbeat. */
    if (bag.state == 1U || !(bag.value & MOWER_BAG_CONNECTED)) bag_full.state=0U;
}
static void receive_bag_full(uint16_t connection, uint16_t flags, uint32_t now) {
    if (!g_exp3_hil.bound || !bag.state || connection != bag.value) return;
    if (!(connection & MOWER_BAG_CONNECTED) || !(flags & MOWER_BAG_FULL_VALID)) {
        bag_full.state=0U;
        return;
    }
    int step=sample_observe(&bag_full, now, connection>>2, MOWER_BAG_HEARTBEAT_MASK);
    if (step > 0) bag_full.value=flags;
    else if (!step) bag_full.value |= flags & MOWER_BAG_FULL;
}
bool exp3_board_runtime(uint32_t now, uint16_t *word) {
    if (!word || !sample_fresh(&runtime, now)) return false;
    *word = runtime.value;
    return true;
}
bool exp3_board_authority(uint32_t now, uint16_t *word) {
    uint16_t ignored;
    if (!word || !exp3_board_runtime(now, &ignored)) return false;
    *word = authority_word;
    return true;
}
static void receive_runtime(uint32_t snapshot, uint32_t now) {
    uint16_t word = (uint16_t)snapshot;
    uint16_t authority = (uint16_t)(snapshot >> 16);
    if (!g_exp3_hil.bound || !(word&8U) ||
        ((word&1U) && !(word&6U)) || (!(word&1U) && (word&6U)) ||
        ((word & MOWER_FAST_STOP_MASK) && (word & (MOWER_FAST_RUN | 7U))) ||
        ((word & MOWER_FAST_EMERGENCY) && !(word & MOWER_FAST_LOCKED)) ||
        ((word & 7U) && !(word & MOWER_FAST_RUN))) return;
    int step=sample_observe(&runtime, now, word>>MOWER_FAST_HEARTBEAT_SHIFT, MOWER_FAST_HEARTBEAT_MASK);
    if (step > 0) {
        runtime.value=word; authority_word=authority;
    } else if (!step) {
        /* Accumulate restrictive flags; a delayed old "enabled/unlocked" packet
         * cannot undo a stop. Only a forward heartbeat may relax restrictions. */
        const uint16_t restrictive = MOWER_FAST_STOP_MASK | MOWER_FAST_FAULT | MOWER_FAST_WHEEL_STALE;
        uint16_t flags = (runtime.value | word) & restrictive;
        uint16_t run = (runtime.value & word) & MOWER_FAST_RUN;
        uint16_t blade = ((runtime.value & 1U) && (word & 1U)) ? word & 7U : 0U;
        if (!run || (flags & MOWER_FAST_STOP_MASK)) blade=run=0U;
        runtime.value = (uint16_t)((runtime.value & MOWER_FAST_HEARTBEAT_WORD_MASK) | MOWER_FAST_VALID | flags | run | blade);
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
#if MOWER_BLADE_OK_ENABLED
    if ((word & MOWER_BATTERY_DIAG_KIND) == MOWER_BATTERY_DIAG_MIN ||
        (word & MOWER_BATTERY_DIAG_KIND) == MOWER_BATTERY_DIAG_REASON) {
        receive_battery(word, now);
        return;
    }
#endif
    if (auto_started && exp3_auto_is_word(word)) {
        if (exp3_auto_receive(&auto_session, word, now, 5000U) == 1) {
            g_exp3_mailbox.command = 0U;
#if MOWER_BLADE_OK_ENABLED
            runtime.state=0U;
            bag.state=bag_full.state=height.state=0U;
            completion_rx = (Exp3CompletionRx){0};
            battery.seen=battery.pending=battery.valid=0U;
#endif
            exp3_hil_bind_session(&g_exp3_hil, NULL, auto_session.peer,
                                  auto_session.self, now);
        }
    } else
        exp3_hil_receive(&g_exp3_hil, NULL, word, now);
}
#else
static void receive_word(uint16_t word, uint32_t now) {
#if MOWER_BLADE_OK_ENABLED
    if ((word & MOWER_BATTERY_DIAG_KIND) == MOWER_BATTERY_DIAG_MIN ||
        (word & MOWER_BATTERY_DIAG_KIND) == MOWER_BATTERY_DIAG_REASON) {
        receive_battery(word, now);
        return;
    }
#endif
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
#if MOWER_BLADE_OK_ENABLED
    completion_rx = (Exp3CompletionRx){0};
#endif
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
#if MOWER_BLADE_OK_ENABLED
void exp3_board_restart_session(uint32_t now) {
    (void)now;
    g_exp3_hil.bound = 0U;
    g_exp3_hil.cancel_failed = g_exp3_hil.restart_requested = 1U;
}
static void restart_session_now(uint32_t now) {
    g_exp3_hil.restart_requested = 0U;
    uint32_t identity = g_exp3_hil.hmi + 1U;
    if (!identity) identity = 1U;
    g_exp3_hil.bound = g_exp3_hil.waiting = 0U;
    g_exp3_mailbox.command = 0U;
    exp3_link_init(&g_exp3_hil.link);
    runtime.state = 0U;
    completion_rx = (Exp3CompletionRx){0};
    g_exp3_hil.cancel_failed = 1U;
#if EXP3_AUTO_SESSION_ENABLED
    exp3_auto_init(&auto_session, EXP3_AUTO_HMI, identity, now);
    auto_started = 1U;
#else
    (void)now;
#endif
}
#endif
void exp3_board_poll(uint32_t now) {
#if MOWER_BLADE_OK_ENABLED
    if (g_exp3_hil.restart_requested) restart_session_now(now);
    if (g_exp3_hil.bound && g_exp3_hil.waiting &&
        (uint32_t)(now-g_exp3_hil.request_started) >= MOWER_AUTH_LIFETIME_MS)
        restart_session_now(now);
#endif
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
/* Keep slot dispatch separate from the session poller's register allocation. */
__attribute__((noinline))
void exp3_board_rf(const uint8_t *p, unsigned n, uint32_t now) {
    unsigned i;
    if (n < 10U || (p[0] != 0xaa && p[0] != 0xac)) return;
    const unsigned header=p[0] == 0xac ? 3U : 2U;
    /* AA and AC share slot validation and dispatch; only the header differs. */
    for (i=9U; i+header<=n && i<37U && p[i]!=0xff;) {
        const unsigned size=header == 3U ? p[i+2] : 2U;
        if (size > n-i-header) break;
        const uint8_t *value=p+i+header;
        if (size == 2U) {
            uint16_t word=(uint16_t)(value[0] | ((uint16_t)value[1]<<8));
#if MOWER_BLADE_OK_ENABLED
            /* A legacy two-byte E9 cannot establish profile-3 authority. */
            if (p[i] == MOWER_HEIGHT_SENS_TYPE && p[i+1] == MOWER_HEIGHT_SENS_ADDRESS)
                receive_height(word, now);
            if (p[i] == MOWER_BAG_SENS_TYPE && p[i+1] == MOWER_BAG_SENS_ADDRESS)
                receive_bag_connection(word, now);
#endif
            if (p[i] == 0xe1 && p[i+1] == 9U) receive_word(word, now);
        }
#if MOWER_BLADE_OK_ENABLED
        else if (size == 4U) {
            uint32_t word=(uint32_t)value[0] | ((uint32_t)value[1]<<8) |
                ((uint32_t)value[2]<<16) | ((uint32_t)value[3]<<24);
            if (p[i] >= EXP3_COMPLETION_TYPE && p[i] < EXP3_COMPLETION_TYPE+EXP3_COMPLETION_PARTS &&
                p[i+1] == EXP3_COMPLETION_ADDRESS+p[i]-EXP3_COMPLETION_TYPE)
                receive_completion(p[i]-EXP3_COMPLETION_TYPE, word, now);
            if (p[i] == MOWER_FAST_STATUS_TYPE && p[i+1] == 10U)
                receive_runtime(word, now);
            if (p[i] == MOWER_BAG_SENS_TYPE && p[i+1] == MOWER_BAG_SENS_ADDRESS) {
                uint16_t connection=(uint16_t)word, flags=(uint16_t)(word>>16);
                if (!(flags & (uint16_t)~MOWER_BAG_FULL_FLAGS_MASK)) {
                    receive_bag_connection(connection, now);
                    receive_bag_full(connection, flags, now);
                }
            }
        }
#endif
        i+=header+size;
    }
}
#endif
