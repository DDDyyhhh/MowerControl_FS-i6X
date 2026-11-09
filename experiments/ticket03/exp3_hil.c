#include "exp3_hil.h"
#if MOWER_BLADE_OK_ENABLED
#include "exp3_board.h"
#endif
#if EXP3_ENABLED
/* Experiment dwell/deadline only; not a production loss or safety policy. */
#define EXP3_TIMEOUT 60000UL
void exp3_hil_trace(Exp3Hil *h, uint32_t now, uint32_t event, uint32_t a, uint32_t b) {
    volatile Exp3Event *e;
#if MOWER_BLADE_OK_ENABLED
    (void)h; (void)now; (void)event; (void)a; (void)b;
    (void)e;
    return; /* Release candidate has no unconsumed debugger trace ring. */
#endif
    if (!h->bound) return;
    if ((uint32_t)(h->log_write - h->log_read) >= EXP3_LOG_COUNT) {
        if (h->log_overflow != UINT32_MAX)
            ++h->log_overflow;
        return;
    }
    e = &h->events[h->log_write % EXP3_LOG_COUNT];
    e->tick = now;
    e->a = a;
    e->b = b;
    e->event = event;
    ++h->log_write;
}
static void log_identity(Exp3Hil *h, uint32_t now, const Exp3Message *m) {
    exp3_hil_trace(h, now, E3_ID_SESSIONS, m->controller, m->hmi);
    exp3_hil_trace(h, now, E3_ID_ACTION, m->request, m->action);
    exp3_hil_trace(h, now, E3_ID_CONTEXT, m->context, m->generation);
    exp3_hil_trace(h, now, E3_DECISION,
                   ((uint32_t)m->target << 16) | ((uint32_t)m->kind << 8) | m->command, m->status);
}
void exp3_hil_bind_session(Exp3Hil *h, Exp3Endpoint *endpoint, uint32_t controller,
                           uint32_t hmi, uint32_t now) {
    if (!h || !controller || !hmi ||
        (h->bound && h->controller == controller && h->hmi == hmi))
        return;
    exp3_link_init(&h->link);
    h->pending = (Exp3Message){0};
    h->last_result = (Exp3Message){0};
    h->waiting = 0U;
#if MOWER_BLADE_OK_ENABLED
    h->cancel_failed = h->cancel_retries = 0U;
#endif
    h->published_word = 0U;
    h->controller = controller;
    h->hmi = hmi;
    h->bound = 1U;
    if (endpoint)
        exp3_bind(endpoint, controller, hmi);
    exp3_hil_trace(h, now, E3_BIND, controller, hmi);
}
void exp3_hil_poll(Exp3Hil *h, volatile Exp3Mailbox *box, Exp3Endpoint *endpoint, uint32_t now,
                   uint32_t dwell) {
    uint32_t cmd = box->command;
    uint16_t word;
    if (cmd) {
        Exp3Message m = box->message;
        box->command = 0;
        if (cmd == EXP3_ARM && !h->bound && m.controller && m.hmi) {
            exp3_hil_bind_session(h, endpoint, m.controller, m.hmi, now);
        } else if (cmd == EXP3_SEND && !endpoint && h->bound && !h->waiting &&
                   m.kind != EXP3_RESULT && m.controller == h->controller && m.hmi == h->hmi &&
                   exp3_link_send(&h->link, &m, now)) {
            h->pending = m;
            h->waiting = 1;
#if MOWER_BLADE_OK_ENABLED
            h->request_started = now;
            h->cancel_failed = h->cancel_retries = 0U;
#endif
            exp3_hil_trace(h, now, E3_TX, h->link.transfer, m.kind);
            log_identity(h, now, &m);
        } else
            exp3_hil_trace(h, now, E3_REJECT, cmd, 0);
    }
    if (exp3_expire(&h->link.rx, now, EXP3_TIMEOUT))
        exp3_hil_trace(h, now, E3_TIMEOUT, 1, 0);
#if MOWER_BLADE_OK_ENABLED
    if (h->waiting && h->pending.kind == EXP3_CANCEL) {
        if ((uint32_t)(now - h->request_started) >= EXP3_TIMEOUT)
            h->cancel_failed = 1U; // fail closed; only matching receipt/rebind clears busy
        else if (!h->cancel_failed && !h->link.tx_active &&
                 (!h->link.raw_active || (uint32_t)(now-h->link.raw_started) >= 6000U) &&
                 h->cancel_retries < 2U &&
                 (uint32_t)(now - h->link.sent_at) >= 8000U) {
            if (exp3_link_send(&h->link, &h->pending, now)) ++h->cancel_retries;
        }
    } else if (h->waiting && (uint32_t)(now - h->request_started) >= EXP3_TIMEOUT) {
        h->waiting = 0U;
    }
#else
    if (h->waiting && (uint32_t)(now - h->link.sent_at) >= EXP3_TIMEOUT) {
        h->waiting = 0;
        exp3_hil_trace(h, now, E3_TIMEOUT, h->pending.action, 0);
    }
#endif
    word = h->bound ? exp3_link_word(&h->link, now, dwell) : 0;
    if (word != h->published_word)
        exp3_hil_trace(h, now, E3_RAW_TX, word, h->link.tx_index);
    h->published_word = word;
}
void exp3_hil_channels(Exp3Hil *h, Exp3Endpoint *endpoint, uint16_t a, uint16_t b, uint32_t now) {
    uint16_t word = 0;
    exp3_hil_trace(h, now, E3_CHANNELS, a, b);
    if (!exp3_channel_word(a, b, &word))
        word = 0;
    exp3_hil_receive(h, endpoint, word, now);
}
void exp3_hil_receive(Exp3Hil *h, Exp3Endpoint *endpoint, uint16_t word, uint32_t now) {
    Exp3Message m;
    int rc;
    exp3_hil_trace(h, now, E3_RAW_RX, word, 0);
    if (!h->bound)
        return;
    rc = exp3_link_receive(&h->link, word, now, EXP3_TIMEOUT, &m);
    if (rc == EXP3_MORE)
        return;
    if (rc != EXP3_COMPLETE) {
        exp3_hil_trace(h, now, rc == EXP3_DUPLICATE ? E3_DUPLICATE : E3_REJECT, word, 0);
        return;
    }
    exp3_hil_trace(h, now, E3_COMPLETE, h->link.rx.transfer, 0);
    log_identity(h, now, &m);
    if (endpoint) {
        Exp3Message result;
        if (!exp3_decide(endpoint, &m, &result)) {
            exp3_hil_trace(h, now, E3_REJECT, m.kind, 0);
            return;
        }
#if MOWER_BLADE_OK_ENABLED
        if (result.context != 0x424c4144UL && result.context != MOWER_CHASSIS_CONTEXT)
            result.status=EXP3_UNKNOWN;
#endif
        h->last_result = result;
        log_identity(h, now, &result);
#if MOWER_BLADE_OK_ENABLED
        if (exp3_board_defer_result(&result))
            return;
#endif
#if defined(EXP3_PWM_PROBE_ENABLED) && EXP3_PWM_PROBE_ENABLED
        if (!exp3_link_send_result(&h->link, &result, now))
#else
        if (!exp3_link_send(&h->link, &result, now))
#endif
            exp3_hil_trace(h, now, E3_BUSY, result.action, 0);
        else
            exp3_hil_trace(h, now, E3_TX, h->link.transfer, result.command);
    } else if (h->waiting && m.kind == EXP3_RESULT && m.command == h->pending.kind &&
               m.controller == h->pending.controller && m.hmi == h->pending.hmi &&
               m.request == h->pending.request && m.action == h->pending.action &&
               m.target == h->pending.target && m.context == h->pending.context &&
               m.generation == h->pending.generation) {
#if MOWER_BLADE_OK_ENABLED
        if (h->pending.kind == EXP3_CANCEL && m.status != EXP3_CANCELLED) {
            h->last_result = m;
            h->cancel_failed = 1U;
            return; // matching identity alone is not a successful stop acknowledgement
        }
#endif
        h->last_result = m;
        h->rtt_ms = now - h->link.sent_at;
        ++h->matches;
        h->waiting = 0;
#if MOWER_BLADE_OK_ENABLED
        h->cancel_failed = 0U;
#endif
        exp3_hil_trace(h, now, E3_RTT, h->rtt_ms, m.action);
    } else
        exp3_hil_trace(h, now, E3_REJECT, m.action, m.kind);
}
#endif
