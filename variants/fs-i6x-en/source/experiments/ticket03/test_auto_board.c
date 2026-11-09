#include "exp3_board.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>

static uint32_t fake_now;
uint32_t HAL_GetTick(void) { return fake_now; }
uint32_t HAL_GetUIDw0(void) { return 0x12345678U; }
uint32_t HAL_GetUIDw1(void) { return 0x9abcdef0U; }
struct FakeSysTick fake_systick = {17U};

static void run_pair(Exp3AutoSession *hmi, uint32_t start, uint32_t end)
{
    uint32_t now;
    for (now = start; now < end; now += 10U) {
        uint16_t word = exp3_auto_word(hmi, now, 20U, 2000U);
        uint16_t ch11 = 1500U, ch12 = 1500U;
        fake_now = now;
        if (word)
            exp3_channels(word, &ch11, &ch12);
        exp3_board_channels(now, 1500U, ch11, ch12);
        exp3_board_poll(now);
        if (now % 120U == 0U)
            (void)exp3_auto_receive(hmi, g_exp3_downlink_word, now, 5000U);
    }
}

static void bootstrap_frame_does_not_hide_ch4_rearm(void)
{
    Exp3Message result = {0};
    Exp3PwmEvent event;
    uint16_t ch11 = 1500U, ch12 = 1500U;
    uint16_t bootstrap = 0xc800U;

    result.kind = EXP3_RESULT;
    result.sender = 2U;
    result.command = EXP3_CONFIRM;
    result.status = EXP3_ACCEPTED;
    result.controller = g_exp3_hil.controller;
    result.hmi = g_exp3_hil.hmi;
    result.action = 1U;
    result.request = 1U;
    result.generation = 1U;
    result.context = EXP3_PWM_CONTEXT;
    result.target = EXP3_PWM_TARGET;

    exp3_board_channels(20010U, 1500U, 1500U, 1500U);
    exp3_board_channels(20020U, 988U, 1500U, 1500U);
    g_exp3_hil.last_result = result;
    exp3_board_channels(20030U, 988U, 1500U, 1500U);
    assert(exp3_board_take_pwm_event(&event));
    assert(event.kind == EXP3_PWM_CONFIRM && event.ch4_epoch == 1U);

    /* An automatic-session word can coincide with the only center sample. */
    exp3_channels(bootstrap, &ch11, &ch12);
    exp3_board_channels(20040U, 1500U, ch11, ch12);
    exp3_board_channels(20050U, 988U, 1500U, 1500U);
    assert(exp3_board_ch4_epoch() == 2U);
    result.action = 2U;
    result.request = 2U;
    result.generation = 2U;
    g_exp3_hil.last_result = result;
    exp3_board_channels(20060U, 988U, 1500U, 1500U);
    assert(exp3_board_take_pwm_event(&event));
    assert(event.kind == EXP3_PWM_CONFIRM && event.ch4_epoch == 2U);
}

static void confirm_and_cancel_complete_within_seven_seconds(void)
{
    Exp3Hil hmi = {0};
    Exp3Mailbox box = {0};
    uint32_t start, now;
    unsigned phase;
    Exp3PwmEvent event;
    exp3_hil_bind_session(&hmi, NULL, g_exp3_hil.controller, g_exp3_hil.hmi, 30000U);
    box.message.controller = hmi.controller;
    box.message.hmi = hmi.hmi;
    box.message.request = box.message.action = 3U;
    box.message.generation = 3U;
    box.message.context = EXP3_PWM_CONTEXT;
    box.message.target = EXP3_PWM_TARGET;
    box.message.sender = 1U;
    for (phase = 0U; phase < 2U; ++phase) {
        start = 30000U + phase * 8000U;
        box.message.kind = phase ? EXP3_CANCEL : EXP3_CONFIRM;
        box.command = EXP3_SEND;
        for (now = start; now < start + 7000U && hmi.matches <= phase; now += 4U) {
            uint16_t a = 1500U, b = 1500U;
            fake_now = now;
            exp3_hil_poll(&hmi, &box, NULL, now, 20U);
            if (hmi.published_word)
                exp3_channels(hmi.published_word, &a, &b);
            exp3_board_channels(now, phase ? 1500U : 988U, a, b);
            exp3_board_poll(now);
            if (now % 120U == 0U)
                exp3_hil_receive(&hmi, NULL, g_exp3_downlink_word, now);
            (void)exp3_board_take_pwm_event(&event);
        }
        assert(hmi.matches == phase + 1U);
        assert(!hmi.waiting);
        assert(hmi.last_result.command == box.message.kind);
        assert(hmi.last_result.status == (phase ? EXP3_CANCELLED : EXP3_ACCEPTED));
        printf("PASS bench %s reply RTT %lu ms with 120 ms sampling\n",
               phase ? "Cancel" : "Confirm", (unsigned long)(now - start));
    }
}

int main(void)
{
    Exp3AutoSession hmi;
    Exp3PwmEvent event;
    uint32_t first_controller;
    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x11112222U, 0U);
    run_pair(&hmi, 0U, 10000U);
    assert(g_exp3_hil.bound && g_exp3_hil.hmi == hmi.self);
    assert(hmi.peer == g_exp3_hil.controller);
    first_controller = hmi.peer;
    assert(exp3_board_take_pwm_event(&event) && event.kind == EXP3_PWM_CANCEL);
    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x33334444U, 10000U);
    run_pair(&hmi, 10000U, 20000U);
    assert(g_exp3_hil.hmi == hmi.self && hmi.peer == first_controller);
    assert(exp3_board_take_pwm_event(&event) && event.kind == EXP3_PWM_CANCEL);
    bootstrap_frame_does_not_hide_ch4_rearm();
    confirm_and_cancel_complete_within_seven_seconds();
    puts("PASS STM32 adapter auto binds and invalidates on remote session change");
    return 0;
}
