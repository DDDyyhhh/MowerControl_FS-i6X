#include "control_source.h"
#include <string.h>

static bool newer(uint32_t a, uint32_t b) { return (int32_t)(a-b) > 0; }

bool HostControl_Publish(HostControlSnapshot_t *s, const uint8_t *p,
                         uint8_t length, uint32_t received_at_ms)
{
    if (!s || !p || length != 8U || p[0] > CONTROL_MODE_API_RUN) return false;
    if (++s->sequence == 0U) ++s->sequence;
    s->received_at_ms = received_at_ms;
    s->mode = p[0]; s->vx = (int8_t)p[1]; s->omega = (int8_t)p[3];
    s->blade = p[4] > 3U ? 3U : p[4];
    s->height = p[5] < 1U ? 1U : p[5] > 5U ? 5U : p[5];
    s->bag = p[6] != 0U;
    if (p[0] != CONTROL_MODE_API_RUN) s->halt_sequence = s->sequence;
    if (p[0] == CONTROL_MODE_REMOTE) s->exit_sequence = s->sequence;
    if (p[0] == CONTROL_MODE_API_STANDBY) {
        s->standby_sequence = s->sequence; s->standby_at_ms = received_at_ms;
    }
    /* Byte7 is deliberately not an authorization input. */
    s->valid = true;
    return true;
}

static void stop(ControlSource_t *s)
{
    s->stop = true; ++s->invalidation_sequence;
    s->remote_ready = false; s->api_ready = false; s->center_tracking = false;
    memset(&s->command, 0, sizeof(s->command));
}

static void fence(ControlSource_t *s, const ControlSourceInput_t *i,
                  const HostControlSnapshot_t *h)
{
    s->barrier_ms = i->now_ms; s->barrier_sequence = h->sequence;
}

void ControlSource_Reset(ControlSource_t *s) { memset(s, 0, sizeof(*s)); }

bool ControlSource_CanUnlock(const ControlSource_t *s)
{
    return s->valid && s->requested == CONTROL_SOURCE_REMOTE &&
        s->mode == CONTROL_MODE_REMOTE;
}

void ControlSource_Advance(ControlSource_t *s, const ControlSourceInput_t *i,
                           const HostControlSnapshot_t *h)
{
    bool frame = !s->frame_seen || i->frame_sequence != s->last_frame;
    bool centered = i->steering >= 1450U && i->steering <= 1550U &&
                    i->throttle >= 1450U && i->throttle <= 1550U;
    bool host_new = h->valid && h->sequence != s->last_host_sequence;
    bool host_fresh = h->valid &&
        (uint32_t)(i->now_ms-h->received_at_ms) < CONTROL_API_TIMEOUT_MS;
    uint8_t sample = i->link_ok ? ControlSource_DecodeCh8(i->ch8) : CONTROL_SOURCE_UNKNOWN;
    s->stop = s->revoke = false;
    /* Comm may have observed a switch excursion between Motion snapshots. */
    if (i->mode_events != s->last_mode_events) {
        s->last_mode_events = i->mode_events;
        stop(s); s->valid = false; s->candidate_count = 0U;
        s->mode = s->requested == CONTROL_SOURCE_API ? CONTROL_MODE_API_STANDBY : CONTROL_MODE_REMOTE;
        fence(s, i, h);
    }

    if (sample == CONTROL_SOURCE_UNKNOWN) {
        if (s->valid || s->remote_ready || s->api_ready) stop(s);
        s->valid = false; s->candidate_count = 0U;
        s->candidate = CONTROL_SOURCE_UNKNOWN;
        s->mode = s->requested == CONTROL_SOURCE_API ? CONTROL_MODE_API_STANDBY : CONTROL_MODE_REMOTE;
        fence(s, i, h);
    } else if (frame) {
        if (sample != s->candidate) { s->candidate = sample; s->candidate_count = 1U; }
        else if (s->candidate_count < 3U) ++s->candidate_count;
        if (s->candidate_count >= 3U && (!s->valid || sample != s->requested)) {
            s->valid = true; s->requested = sample;
            if (sample == CONTROL_SOURCE_REMOTE) s->takeover = false;
            s->mode = sample == CONTROL_SOURCE_API && !s->takeover ?
                CONTROL_MODE_API_STANDBY : CONTROL_MODE_REMOTE;
            stop(s); fence(s, i, h);
        }
    }
    if (frame) { s->frame_seen = true; s->last_frame = i->frame_sequence; }

    if (i->authorized && !s->was_authorized) fence(s, i, h);
    if (!i->authorized) {
        if (s->was_authorized) stop(s);
        if (s->mode == CONTROL_MODE_API_RUN) s->mode = CONTROL_MODE_API_STANDBY;
        s->remote_ready = s->api_ready = false;
        fence(s, i, h);
    }
    s->was_authorized = i->authorized;

    if (s->valid && s->requested == CONTROL_SOURCE_API && !s->takeover && !centered) {
        s->takeover = true; s->mode = CONTROL_MODE_REMOTE;
        stop(s); fence(s, i, h);
    }

    if (s->valid && !s->stop) {
        if (s->mode == CONTROL_MODE_REMOTE) {
            if (!s->remote_ready && frame) {
                if (!centered) s->center_tracking = false;
                else if (!s->center_tracking) {
                    s->center_tracking = true; s->center_since = i->frame_at_ms;
                } else if ((uint32_t)(i->frame_at_ms-s->center_since) >= 200U) {
                    s->remote_ready = i->authorized;
                }
            }
        } else if (!i->authorized) {
            /* A selector/host handshake never grants safety authorization. */
        } else if (s->mode == CONTROL_MODE_API_RUN) {
            if (!host_fresh) {
                stop(s); s->revoke = true; s->takeover = true;
                s->mode = CONTROL_MODE_REMOTE; fence(s, i, h);
            } else if (h->halt_sequence != s->last_halt_sequence) {
                stop(s); s->mode = CONTROL_MODE_API_STANDBY;
            } else if (h->mode == CONTROL_MODE_API_RUN &&
                       newer(h->sequence, s->activation_sequence)) {
                s->api_ready = true; s->command = *h;
            }
        } else if (host_new && host_fresh && centered &&
                   h->mode == CONTROL_MODE_API_RUN && h->standby_sequence != 0U &&
                   newer(h->sequence, h->standby_sequence) &&
                   newer(h->standby_sequence, s->barrier_sequence) &&
                   (h->exit_sequence == 0U || newer(h->standby_sequence, h->exit_sequence)) &&
                   newer(h->standby_at_ms, s->barrier_ms) &&
                   (uint32_t)(i->now_ms-h->standby_at_ms) < CONTROL_API_TIMEOUT_MS) {
            s->mode = CONTROL_MODE_API_RUN; s->activation_sequence = h->sequence;
            stop(s); /* The entering frame never powers actuators. */
        }
    }
    s->last_host_sequence = h->sequence; s->last_halt_sequence = h->halt_sequence;
    s->display = !s->valid ? CONTROL_DISPLAY_UNKNOWN : s->takeover ? CONTROL_DISPLAY_TAKEOVER :
        s->mode == CONTROL_MODE_API_RUN ? CONTROL_DISPLAY_API_RUN :
        s->mode == CONTROL_MODE_API_STANDBY ? CONTROL_DISPLAY_API_STANDBY : CONTROL_DISPLAY_REMOTE;
}
