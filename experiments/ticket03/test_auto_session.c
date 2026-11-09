#include "exp3_auto_session.h"
#include "exp3_link.h"
#include "exp3_hil.h"
#include <assert.h>
#include <stdio.h>

static void exchange(Exp3AutoSession *hmi, Exp3AutoSession *controller,
                     uint32_t start_ms, uint32_t until_ms, int drop_downlink)
{
    uint32_t now;
    for (now = start_ms; now < until_ms; now += 10U) {
        uint16_t up = exp3_auto_word(hmi, now, 20U, 200U);
        uint16_t down = exp3_auto_word(controller, now, 250U, 200U);
        if (up)
            (void)exp3_auto_receive(controller, up, now, 5000U);
        if (down && !drop_downlink)
            (void)exp3_auto_receive(hmi, down, now, 5000U);
    }
}

static void automatic_pairing_and_reboot(void)
{
    Exp3AutoSession hmi, controller;
    Exp3Endpoint endpoint;
    Exp3Message confirm = {0}, result;
    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x12345678U, 0U);
    exp3_auto_init(&controller, EXP3_AUTO_CONTROLLER, 0xabcdef12U, 0U);
    exchange(&hmi, &controller, 0U, 10000U, 0);
    assert(hmi.peer == controller.self);
    assert(controller.peer == hmi.self);
    exp3_bind(&endpoint, controller.self, hmi.self);
    confirm.controller = controller.self;
    confirm.hmi = hmi.self;
    confirm.request = confirm.action = confirm.generation = confirm.target = 1U;
    confirm.kind = EXP3_CONFIRM;
    confirm.sender = 1U;
    assert(exp3_decide(&endpoint, &confirm, &result));
    assert(result.status == EXP3_ACCEPTED);

    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x12345679U, 10000U);
    exchange(&hmi, &controller, 10000U, 20000U, 0);
    assert(controller.peer == hmi.self);
    exp3_bind(&endpoint, controller.self, hmi.self);
    assert(exp3_decide(&endpoint, &confirm, &result));
    assert(result.status == EXP3_SESSION);

    exp3_auto_init(&controller, EXP3_AUTO_CONTROLLER, 0xabcdef13U, 20000U);
    exchange(&hmi, &controller, 20000U, 30000U, 0);
    assert(hmi.peer == controller.self);
    assert(controller.peer == hmi.self);
}

static void loss_static_and_order_never_bind(void)
{
    Exp3AutoSession hmi, controller;
    uint16_t words[5], a, b, invalid;
    unsigned i;
    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x11223344U, 0U);
    exp3_auto_init(&controller, EXP3_AUTO_CONTROLLER, 0x55667788U, 0U);
    for (i = 0; i < 5; ++i)
        words[i] = exp3_auto_word(&hmi, i * 20U, 20U, 200U);
    exp3_channels(words[0], &a, &b);
    assert(exp3_channel_raw_word(a, b, &invalid) && invalid == words[0]);
    assert(!exp3_channel_raw_word(1500U, 1500U, &invalid));
    for (i = 0; i < 100; ++i)
        assert(exp3_auto_receive(&controller, words[0], i * 10U, 5000U) == 0);
    assert(controller.peer == 0U);
    assert(exp3_auto_receive(&controller, words[2], 1010U, 5000U) == 0);
    for (i = 0; i < 4; ++i)
        (void)exp3_auto_receive(&controller, words[i], 1020U + i * 10U, 5000U);
    assert(controller.peer == 0U);
    assert(exp3_auto_receive(&controller, words[4] ^ 1U, 1100U, 5000U) == 0);
    assert(controller.peer == 0U);
    exchange(&hmi, &controller, 1000U, 10000U, 1);
    assert(controller.peer == hmi.self);
    assert(hmi.peer == 0U);
}

static void session_change_discards_old_transfer_and_result(void)
{
    Exp3Hil hil = {0};
    Exp3Endpoint endpoint;
    Exp3Message old = {0}, result;
    exp3_hil_bind_session(&hil, &endpoint, 11U, 22U, 100U);
    old.controller = 11U;
    old.hmi = 22U;
    old.request = old.action = old.generation = old.target = 1U;
    old.kind = EXP3_CONFIRM;
    old.sender = 1U;
    assert(exp3_decide(&endpoint, &old, &result));
    assert(result.status == EXP3_ACCEPTED);
    hil.last_result = result;
    hil.waiting = 1U;
    hil.link.rx.transfer = 77U;
    exp3_hil_bind_session(&hil, &endpoint, 11U, 23U, 200U);
    assert(!hil.waiting && hil.last_result.kind == 0U);
    assert(hil.link.rx.transfer == 0U && endpoint.created == 0U);
    assert(exp3_decide(&endpoint, &old, &result));
    assert(result.status == EXP3_SESSION);
}

static void paired_sessions_complete_confirm_result(void)
{
    Exp3AutoSession hmi_boot, controller_boot;
    Exp3Hil hmi = {0}, controller = {0};
    Exp3Endpoint endpoint;
    Exp3Mailbox hmi_box = {0}, controller_box = {0};
    uint32_t now;
    exp3_auto_init(&hmi_boot, EXP3_AUTO_HMI, 0x90123456U, 0U);
    exp3_auto_init(&controller_boot, EXP3_AUTO_CONTROLLER, 0x789abcdeU, 0U);
    exchange(&hmi_boot, &controller_boot, 0U, 10000U, 0);
    assert(hmi_boot.peer == controller_boot.self);
    exp3_hil_bind_session(&hmi, NULL, hmi_boot.peer, hmi_boot.self, 10000U);
    exp3_hil_bind_session(&controller, &endpoint, controller_boot.self,
                          controller_boot.peer, 10000U);
    hmi_box.message.controller = hmi_boot.peer;
    hmi_box.message.hmi = hmi_boot.self;
    hmi_box.message.request = 1U;
    hmi_box.message.action = 1U;
    hmi_box.message.generation = 1U;
    hmi_box.message.target = 1U;
    hmi_box.message.kind = EXP3_CONFIRM;
    hmi_box.message.sender = 1U;
    hmi_box.command = EXP3_SEND;
    for (now = 10000U; now < 40000U; now += 4U) {
        exp3_hil_poll(&hmi, &hmi_box, NULL, now, 20U);
        exp3_hil_poll(&controller, &controller_box, &endpoint, now, 250U);
        exp3_hil_receive(&controller, &endpoint, hmi.published_word, now);
        if (now % 120U == 0U)
            exp3_hil_receive(&hmi, NULL, controller.published_word, now);
        hmi.log_read = hmi.log_write;
        controller.log_read = controller.log_write;
    }
    assert(endpoint.created == 1U);
    assert(hmi.matches == 1U && hmi.last_result.status == EXP3_ACCEPTED);
}

static void handshake_pacing(void)
{
    Exp3AutoPace hmi = {0}, controller = {0};
    /* Cold start: unbound retries fast; a bind settles even before E9 is fresh. */
    assert(exp3_auto_hmi_interval(&hmi, false, false) == EXP3_AUTO_HMI_FAST_ROUND_MS);
    exp3_auto_hmi_settled(&hmi);
    assert(exp3_auto_hmi_interval(&hmi, true, false) == EXP3_AUTO_ROUND_MS);
    assert(exp3_auto_hmi_interval(&hmi, true, true) == EXP3_AUTO_ROUND_MS);
    /* Chassis powered off for any duration: stay fast until a new bind. */
    assert(exp3_auto_hmi_interval(&hmi, true, false) == EXP3_AUTO_HMI_FAST_ROUND_MS);
    assert(exp3_auto_hmi_interval(&hmi, true, false) == EXP3_AUTO_HMI_FAST_ROUND_MS);
    exp3_auto_hmi_settled(&hmi);
    assert(exp3_auto_hmi_interval(&hmi, true, false) == EXP3_AUTO_ROUND_MS);
    /* A link blip with the same session recovers without a bind. */
    assert(exp3_auto_hmi_interval(&hmi, true, true) == EXP3_AUTO_ROUND_MS);
    assert(exp3_auto_hmi_interval(&hmi, true, false) == EXP3_AUTO_HMI_FAST_ROUND_MS);
    assert(exp3_auto_hmi_interval(&hmi, true, true) == EXP3_AUTO_ROUND_MS);
    /* Controller repeats fast only for a bounded window, across tick wrap. */
    assert(exp3_auto_controller_interval(&controller, 100U) == EXP3_AUTO_ROUND_MS);
    uint32_t bound_at = 0xfffff000U;
    exp3_auto_controller_bound(&controller, bound_at);
    assert(exp3_auto_controller_interval(&controller, bound_at + 5999U) ==
           EXP3_AUTO_CONTROLLER_FAST_ROUND_MS);
    assert(exp3_auto_controller_interval(&controller, bound_at + 6000U) == EXP3_AUTO_ROUND_MS);
    assert(exp3_auto_controller_interval(&controller, bound_at + 6001U) == EXP3_AUTO_ROUND_MS);
}

int main(void)
{
    handshake_pacing();
    automatic_pairing_and_reboot();
    loss_static_and_order_never_bind();
    session_change_discards_old_transfer_and_result();
    paired_sessions_complete_confirm_result();
    puts("PASS automatic paired sessions, reboot, loss, static failsafe and malformed bootstrap");
    return 0;
}
