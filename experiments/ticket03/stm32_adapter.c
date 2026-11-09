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
volatile uint16_t g_blade_ok_runtime_word;
volatile uint16_t g_mower_unlock_generation, g_mower_control_word;
static Exp3PwmProbe chassis_probe;
static volatile Exp3PwmEvent chassis_event;
static volatile uint8_t chassis_ready, rebind_requested;
void exp3_board_request_rebind(void) { rebind_requested=1U; }
bool exp3_board_take_chassis_event(Exp3PwmEvent *event) {
    if (!event || !chassis_ready) return false;
    *event=chassis_event; chassis_ready=0U; return true;
}

#endif
#if EXP3_AUTO_SESSION_ENABLED
static Exp3AutoSession auto_session;
static uint8_t auto_started;
#endif
#if EXP3_PWM_PROBE_ENABLED
static Exp3PwmProbe pwm_probe;
static uint32_t pwm_controller, pwm_hmi;
static volatile uint32_t pwm_ch4_epoch;
static uint8_t pwm_ch4_center_seen, pwm_ch4_start;
static volatile Exp3PwmEvent pwm_event;
static volatile uint8_t pwm_event_ready;
static volatile uint32_t pwm_diag_sequence;
static volatile Exp3PwmDiag pwm_diag;
#endif
#if MOWER_BLADE_OK_ENABLED
bool exp3_board_defer_result(const Exp3Message *result) {
    Exp3PwmEvent event;
    if (release_pending) return true;
    if (result->context != EXP3_PWM_CONTEXT && result->context != MOWER_CHASSIS_CONTEXT) return false;
    event = exp3_pwm_probe_observe(result->context == MOWER_CHASSIS_CONTEXT ? &chassis_probe : &pwm_probe, result);
    if (event.kind == EXP3_PWM_NONE) return false;
    release_result = *result;
    release_pending = 1U;
    event.ch4_epoch = pwm_ch4_epoch;
    if (result->context == MOWER_CHASSIS_CONTEXT) {
        chassis_event=event; chassis_ready=1U;
    } else { pwm_event = event; pwm_event_ready = 1U; }
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
#if MOWER_BLADE_OK_ENABLED && EXP3_AUTO_SESSION_ENABLED
    if (rebind_requested) {
        rebind_requested=0U;
        uint32_t nonce=auto_session.self+1U;
        if (!nonce) nonce=1U;
        exp3_auto_init(&auto_session,EXP3_AUTO_CONTROLLER,nonce,now);
        memset(&g_exp3_hil,0,sizeof(g_exp3_hil));
        release_pending=release_done=chassis_ready=pwm_event_ready=0U;
        g_mower_unlock_generation=g_mower_control_word=0U;
    }
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
    if (release_pending && release_done && !g_exp3_hil.link.tx_active) {
        unsigned i;
        if (!release_accepted) release_result.status = EXP3_UNKNOWN;
        for (i=0U; i<EXP3_CACHE_SIZE; ++i)
            if (endpoint.records[i].action == release_result.action)
                endpoint.records[i].status = release_result.status;
        g_exp3_hil.last_result = release_result;
        if (exp3_link_send_result(&g_exp3_hil.link, &release_result, now)) {
            release_done = 0U;
            release_pending = 0U;
        }
    }
#endif
    exp3_hil_poll(&g_exp3_hil, &g_exp3_mailbox, &endpoint, now, 250);
    g_exp3_downlink_word = g_exp3_hil.published_word;
#if EXP3_AUTO_SESSION_ENABLED
    if (auto_started && !g_exp3_hil.link.tx_active)
        g_exp3_downlink_word = exp3_auto_word(&auto_session, now, 250U, 2000U);
#endif
}
void exp3_board_channels(uint32_t now, uint16_t ch4, uint16_t a, uint16_t b) {
#if EXP3_PWM_PROBE_ENABLED
    uint8_t center, start;
    if (pwm_controller != g_exp3_hil.controller || pwm_hmi != g_exp3_hil.hmi) {
        pwm_controller = g_exp3_hil.controller;
        pwm_hmi = g_exp3_hil.hmi;
        pwm_ch4_epoch = 0U;
        pwm_ch4_center_seen = 0U;
        pwm_ch4_start = 0U;
        exp3_pwm_probe_init(&pwm_probe);
    }
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
            g_exp3_mailbox.command = 0U;
#if MOWER_BLADE_OK_ENABLED
            release_pending = release_done = 0U;
            g_blade_ok_runtime_word = 0U;
            g_mower_unlock_generation=g_mower_control_word=0U;
            chassis_ready=0U;
            exp3_pwm_probe_init(&chassis_probe);
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
    return pwm_ch4_epoch;
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
