#include "exp3_board.h"
#if EXP3_ENABLED
#include "main.h"
#include <string.h>
Exp3Hil g_exp3_hil;
volatile Exp3Mailbox g_exp3_mailbox;
volatile uint16_t g_exp3_downlink_word;
static Exp3Endpoint endpoint;
#if MOWER_BLADE_OK_ENABLED
static Exp3Message release_result;
/* Task_Comm produces; Task_Motion consumes and publishes completion last. */
static volatile uint8_t release_pending, release_done, release_accepted;
static Exp3Message slow_result, completed_result;
static uint8_t slow_ready, completion_sequence;
static uint32_t completion_parts[2][EXP3_COMPLETION_PARTS];
static volatile uint8_t completion_index;
volatile uint16_t g_blade_ok_runtime_word;

#endif
#if EXP3_AUTO_SESSION_ENABLED
static Exp3AutoSession auto_session;
static uint8_t auto_started;
static Exp3AutoPace auto_pace;
#endif
#if EXP3_PWM_PROBE_ENABLED
static Exp3PwmProbe pwm_probe;
#if MOWER_BLADE_OK_ENABLED
static Exp3PwmProbe unlock_probe;
static volatile uint32_t unlock_ch4_epoch;
static uint8_t unlock_center_seen, unlock_ch4_full;
#endif
static uint32_t pwm_controller, pwm_hmi;
static volatile uint32_t pwm_ch4_epoch;
static uint8_t pwm_ch4_center_seen, pwm_ch4_start;
static volatile Exp3PwmEvent pwm_event;
static volatile uint8_t pwm_event_ready;
static volatile uint32_t pwm_diag_sequence;
static volatile Exp3PwmDiag pwm_diag;
#endif
#if MOWER_BLADE_OK_ENABLED
static void reset_completions(void);
void exp3_board_restart_session(uint32_t now) {
    (void)now;
    g_exp3_hil.bound = 0U;
    g_exp3_hil.restart_requested = 1U;
}
static void restart_session_now(uint32_t now) {
    g_exp3_hil.restart_requested = 0U;
    uint32_t identity = g_exp3_hil.controller + 1U;
    if (!identity) identity = 1U;
    g_exp3_hil.bound = g_exp3_hil.waiting = 0U;
    g_exp3_hil.token_active = g_exp3_hil.token_armed = 0U;
    g_exp3_mailbox.command = 0U;
    reset_completions();
    exp3_link_init(&g_exp3_hil.link);
    g_exp3_downlink_word = 0U;
#if EXP3_AUTO_SESSION_ENABLED
    exp3_auto_init(&auto_session, EXP3_AUTO_CONTROLLER, identity, now);
    auto_started = 1U;
#else
    (void)now;
#endif
    /* An identity-less cancellation always revokes Motion permission. */
    pwm_event = (Exp3PwmEvent){0}; pwm_event.kind = EXP3_PWM_CANCEL;
    pwm_event_ready = 1U;
}
uint16_t exp3_board_update_intent(unsigned phase, bool unlock, uint32_t request, uint32_t now) {
    Exp3Hil *h = &g_exp3_hil;
    bool same_context = !!(h->authority & MOWER_AUTH_UNLOCK) == unlock;
    if (!h->bound) { phase = MOWER_AUTH_INIT; h->token_armed = 0U; }
    if (!same_context) h->token_active = h->token_armed = 0U;
    if (phase == MOWER_AUTH_CONFIRM) {
        if (h->token_armed) {
            if (h->token == MOWER_AUTH_TOKEN_MASK) {
                exp3_board_restart_session(now); phase = MOWER_AUTH_INIT;
            } else {
                ++h->token; h->token_at = now; h->token_request = request;
                h->token_active = 1U; h->token_armed = 0U;
            }
        }
        if (!h->token_active || request != h->token_request ||
            (uint32_t)(now-h->token_at) >= MOWER_AUTH_LIFETIME_MS) {
            h->token_active = 0U;
            if (phase != MOWER_AUTH_INIT) phase = MOWER_AUTH_CH4;
        }
    } else {
        h->token_active = 0U;
        if (phase == MOWER_AUTH_CENTER) h->token_armed = 1U;
        else if (phase != MOWER_AUTH_CH4) h->token_armed = 0U;
    }
    h->authority = mower_auth_pack(phase, unlock, h->token);
    return h->authority;
}
bool exp3_board_check_intent(const Exp3Message *m, uint32_t now) {
    Exp3Hil *h = &g_exp3_hil;
    h->intent_reason = 0U;
    if (!h->bound || m->controller != h->controller || m->hmi != h->hmi || m->target != 1U)
        h->intent_reason = 1U;
    else if (m->generation != h->token ||
        (m->context != EXP3_UNLOCK_CONTEXT && m->context != EXP3_PWM_CONTEXT) ||
        !!(h->authority & MOWER_AUTH_UNLOCK) != (m->context == EXP3_UNLOCK_CONTEXT))
        h->intent_reason = 2U;
    else if (!h->token_active || (uint32_t)(now-h->token_at) >= MOWER_AUTH_LIFETIME_MS)
        h->intent_reason = 3U;
    if (h->intent_reason) return false;
    h->token_active = 0U; /* Consume before checking/performing the actual operation. */
    return true;
}
static bool same_request(const Exp3Message *a, const Exp3Message *b) {
    return a->controller == b->controller && a->hmi == b->hmi &&
        a->request == b->request && a->action == b->action && a->context == b->context &&
        a->generation == b->generation && a->target == b->target;
}
static void reset_completions(void) {
    release_pending = release_done = slow_ready = 0U;
    completion_sequence = 0U;
    completed_result = (Exp3Message){0};
    memset(completion_parts, 0, sizeof(completion_parts));
    completion_index = 0U;
}
uint32_t exp3_board_completion_part(unsigned part) {
    if (part >= EXP3_COMPLETION_PARTS || !g_exp3_hil.bound ||
        completed_result.controller != g_exp3_hil.controller ||
        completed_result.hmi != g_exp3_hil.hmi) return 0U;
    return completion_parts[completion_index][part];
}
static void publish_completion(const Exp3Message *result) {
    uint8_t bytes[EXP3_COMPLETION_BYTES];
    uint8_t next = completion_index ^ 1U;
    bool compact;
    /* A delayed Confirm/duplicate must not replace a newer completion or the
     * Cancel of that same action. There is only one live requester. */
    if (completed_result.action &&
        (result->action < completed_result.action ||
         (result->action == completed_result.action &&
          completed_result.command == EXP3_CANCEL && result->command != EXP3_CANCEL))) return;
    if (same_request(result, &completed_result) &&
        result->command == completed_result.command && result->status == completed_result.status) {
        slow_result = *result; slow_ready = 1U;
        return;
    }
    completed_result = *result;
    slow_result = *result;
    slow_ready = 1U; /* Coalesce unsent obsolete receipts, never cut a wire frame. */
    if (++completion_sequence == 0U) ++completion_sequence;
    compact = exp3_completion_encode(result, bytes);
    for (unsigned i=0U; i<EXP3_COMPLETION_PARTS; ++i)
        completion_parts[next][i] = compact ?
            exp3_completion_part(bytes, completion_sequence, i) : 0U;
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile ("" ::: "memory");
#endif
    completion_index = next;
}
bool exp3_board_defer_result(const Exp3Message *result) {
    Exp3PwmEvent event;
    if (result->context != EXP3_PWM_CONTEXT && result->context != EXP3_UNLOCK_CONTEXT)
        return false;
    if (result->controller != g_exp3_hil.controller || result->hmi != g_exp3_hil.hmi)
        return false;
    /* The execution slot, unlike the slow sender, is occupied only until
     * Motion completes. Never acknowledge a cached provisional Accepted. */
    if (release_pending) return true;
    event = exp3_pwm_probe_observe(result->context == EXP3_UNLOCK_CONTEXT ?
                                  &unlock_probe : &pwm_probe, result);
    if (event.kind == EXP3_PWM_NONE) {
        publish_completion(result);
        return true;
    }
    release_result = *result;
    release_done = 0U;
    release_pending = 1U;
    event.ch4_epoch = g_exp3_hil.token;
    pwm_event = event;
    pwm_event_ready = 1U;
    return true;
}
#endif
volatile uint32_t g_exp3_capture_overflow;
static volatile struct { uint32_t tick, request, low, high; } capture[4];
static volatile uint8_t capture_write, capture_read;
void exp3_board_status(Exp3BoardStatus *status) {
    if (!status) return;
    status->last_result = g_exp3_hil.last_result;
    status->controller = g_exp3_hil.controller;
    status->hmi = g_exp3_hil.hmi;
    status->matches = g_exp3_hil.matches;
    status->bound = g_exp3_hil.bound;
    status->waiting = g_exp3_hil.waiting;
    status->cancel_ready = status->cancel_failed = 0U;
}
/* USART2 ISR boundary: raw bytes only; decoding/logging stays in Task_Comm. */
void exp3_board_sens(const uint8_t *req, const uint8_t *rsp, unsigned n) {
    unsigned i;
    uint32_t r = 0, lo = 0, hi = n << 16;
    uint8_t slot = capture_write % 4;
    if ((uint8_t)(capture_write - capture_read) >= 4) {
        if (g_exp3_capture_overflow != UINT32_MAX)
            ++g_exp3_capture_overflow;
        return;
    }
    for (i = 0; i < 4; i++) {
        r |= (uint32_t)req[i] << (8 * i);
        lo |= (uint32_t)rsp[i] << (8 * i);
    }
    for (i = 4; i < n && i < 6; i++)
        hi |= (uint32_t)rsp[i] << (8 * (i - 4));
    capture[slot].tick = HAL_GetTick();
    capture[slot].request = r;
    capture[slot].low = lo;
    capture[slot].high = hi;
    ++capture_write;
}
void exp3_board_poll(uint32_t now) {
#if MOWER_BLADE_OK_ENABLED
    if (g_exp3_hil.restart_requested) restart_session_now(now);
#endif
    unsigned budget = 4;
    while (capture_read != capture_write && budget--) {
        uint8_t slot = capture_read % 4;
        exp3_hil_trace(&g_exp3_hil, capture[slot].tick, E3_SENS_REQUEST, capture[slot].request, 0);
        exp3_hil_trace(&g_exp3_hil, capture[slot].tick, E3_SENS_RESPONSE, capture[slot].low,
                       capture[slot].high);
        ++capture_read;
    }
#if MOWER_BLADE_OK_ENABLED
    if (release_pending && release_done) {
        unsigned i;
        if (!release_accepted) release_result.status = EXP3_UNKNOWN;
        for (i=0U; i<EXP3_CACHE_SIZE; ++i)
            if (same_request(&endpoint.records[i], &release_result))
                endpoint.records[i].status = release_result.status;
        g_exp3_hil.last_result = release_result;
        publish_completion(&release_result);
        release_done = 0U;
        release_pending = 0U;
    }
    if (slow_ready && !g_exp3_hil.link.tx_active &&
        exp3_link_send_result(&g_exp3_hil.link, &slow_result, now)) slow_ready = 0U;
#endif
    exp3_hil_poll(&g_exp3_hil, &g_exp3_mailbox, &endpoint, now, 250);
    g_exp3_downlink_word = g_exp3_hil.published_word;
#if EXP3_AUTO_SESSION_ENABLED
    if (auto_started && !g_exp3_hil.link.tx_active)
        g_exp3_downlink_word = exp3_auto_word(&auto_session, now, 250U,
                                               exp3_auto_controller_interval(&auto_pace, now));
#endif
}
void exp3_board_channels(uint32_t now, uint16_t ch4, uint16_t a, uint16_t b) {
#if EXP3_PWM_PROBE_ENABLED
#if !MOWER_BLADE_OK_ENABLED
    uint8_t center, start;
#else
    (void)ch4;
#endif
    if (pwm_controller != g_exp3_hil.controller || pwm_hmi != g_exp3_hil.hmi) {
        pwm_controller = g_exp3_hil.controller;
        pwm_hmi = g_exp3_hil.hmi;
        pwm_ch4_epoch = 0U;
        pwm_ch4_center_seen = 0U;
        pwm_ch4_start = 0U;
        exp3_pwm_probe_init(&pwm_probe);
#if MOWER_BLADE_OK_ENABLED
        exp3_pwm_probe_init(&unlock_probe);
        unlock_ch4_epoch = 0U;
        unlock_center_seen = unlock_ch4_full = 0U;
        reset_completions();
#endif
    }
#if !MOWER_BLADE_OK_ENABLED
    /* A bootstrap word still carries a real CH4 sample. Track the edge before
     * the auto-session parser consumes CH11/12 and returns early. */
    center = ch4 >= 1400U && ch4 <= 1600U;
    start = ch4 >= 900U && ch4 <= 1100U;
    if (center) {
        pwm_ch4_center_seen = 1U;
        pwm_ch4_start = 0U;
    } else if (start) {
        if (pwm_ch4_center_seen && !pwm_ch4_start) {
            ++pwm_ch4_epoch;
            if (pwm_ch4_epoch == 0U)
                ++pwm_ch4_epoch;
            pwm_ch4_center_seen = 0U;
        }
        pwm_ch4_start = 1U;
    } else {
        pwm_ch4_start = 0U;
    }
#if MOWER_BLADE_OK_ENABLED
    if (center) unlock_center_seen = 1U;
    if (start) {
        if (unlock_center_seen && !unlock_ch4_full) {
            if (++unlock_ch4_epoch == 0U) ++unlock_ch4_epoch;
            unlock_center_seen = 0U;
        }
        unlock_ch4_full = 1U;
    } else unlock_ch4_full = 0U;
#endif
#endif
#else
    (void)ch4;
#endif
#if EXP3_AUTO_SESSION_ENABLED
    uint16_t raw_word;
    if (!auto_started) {
        uint32_t seed = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ now ^ SysTick->VAL;
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        exp3_auto_init(&auto_session, EXP3_AUTO_CONTROLLER, seed, now);
        auto_started = 1U;
    }
    if (exp3_channel_raw_word(a, b, &raw_word) && exp3_auto_is_word(raw_word)) {
        exp3_hil_trace(&g_exp3_hil, now, E3_CHANNELS, a, b);
        exp3_hil_trace(&g_exp3_hil, now, E3_RAW_RX, raw_word, 0U);
        if (exp3_auto_receive(&auto_session, raw_word, now, 5000U) == 1) {
            exp3_auto_controller_bound(&auto_pace, now);
            g_exp3_mailbox.command = 0U;
#if MOWER_BLADE_OK_ENABLED
            reset_completions();
            g_blade_ok_runtime_word = 0U;
#endif
            exp3_hil_bind_session(&g_exp3_hil, &endpoint, auto_session.self,
                                  auto_session.peer, now);
#if EXP3_PWM_PROBE_ENABLED
            pwm_event = (Exp3PwmEvent){0};
            pwm_event.kind = EXP3_PWM_CANCEL;
            pwm_event_ready = 1U;
            pwm_ch4_center_seen = 0U;
            pwm_ch4_start = 0U;
#endif
        }
        return;
    }
#endif
    exp3_hil_channels(&g_exp3_hil, &endpoint, a, b, now);
#if EXP3_PWM_PROBE_ENABLED && !MOWER_BLADE_OK_ENABLED
    {
        Exp3PwmEvent event = exp3_pwm_probe_observe(&pwm_probe, &g_exp3_hil.last_result);
        if (event.kind != EXP3_PWM_NONE && !pwm_event_ready) {
            event.ch4_epoch = pwm_ch4_epoch;
            pwm_event = event;
            pwm_event_ready = 1U;
        }
    }
#endif
}
#if EXP3_PWM_PROBE_ENABLED
#if MOWER_BLADE_OK_ENABLED
void exp3_board_complete_pwm_event(const Exp3PwmEvent *event, bool accepted) {
    if (!release_pending || !event || event->message.controller != release_result.controller ||
        event->message.hmi != release_result.hmi || event->message.action != release_result.action ||
        event->message.request != release_result.request ||
        event->message.context != release_result.context ||
        event->message.target != release_result.target ||
        event->message.generation != release_result.generation ||
        event->message.command != release_result.command) return;
    release_accepted = accepted;
    release_done = 1U;
}
#endif
uint32_t exp3_board_ch4_epoch(void) {
#if MOWER_BLADE_OK_ENABLED
    return g_exp3_hil.token;
#else
    return pwm_ch4_epoch;
#endif
}
uint32_t exp3_board_unlock_epoch(void) {
#if MOWER_BLADE_OK_ENABLED
    return g_exp3_hil.token;
#else
    return 0U;
#endif
}
bool exp3_board_take_pwm_event(Exp3PwmEvent *event) {
    if (!event || !pwm_event_ready) return false;
    *event = pwm_event;
    pwm_event_ready = 0U;
    return true;
}
void exp3_board_publish_pwm_diag(const Exp3PwmDiag *diag) {
    if (!diag) return;
    ++pwm_diag_sequence;
    pwm_diag = *diag;
    ++pwm_diag_sequence;
}
void exp3_board_read_pwm_diag(Exp3PwmDiag *diag) {
    uint32_t before, after;
    if (!diag) return;
    do {
        before = pwm_diag_sequence;
        *diag = pwm_diag;
        after = pwm_diag_sequence;
    } while ((before != after) || (before & 1U));
}
#endif
#endif
