#include "exp3_board.h"
#include "main.h"
#include "exp3_blade_adapter.h"
#include "remote_authorization.h"
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


static void verified_controller_reply(void)
{
    Exp3Hil hmi = {0};
    Exp3Mailbox box = {0};
    Exp3PwmEvent event;
    uint32_t now;
    exp3_hil_bind_session(&hmi, NULL, g_exp3_hil.controller, g_exp3_hil.hmi, 10000U);
    box.message = (Exp3Message){.controller=hmi.controller, .hmi=hmi.hmi,
        .request=1U, .action=1U, .generation=1U, .context=EXP3_PWM_CONTEXT,
        .target=1U, .kind=EXP3_CONFIRM, .sender=1U};
    box.command = EXP3_SEND;
    exp3_board_channels(10000U, 1500U, 1500U, 1500U);
    exp3_board_update_intent(MOWER_AUTH_CENTER,false,1U,10000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,false,1U,10004U);
    for (now=10004U; now<12500U; now+=4U) {
        uint16_t a=1500U, b=1500U;
        fake_now=now;
        exp3_hil_poll(&hmi, &box, NULL, now, 20U);
        exp3_channels(hmi.published_word, &a, &b);
        exp3_board_channels(now, 988U, a, b);
        exp3_board_poll(now);
    }
    assert(exp3_board_take_pwm_event(&event));
    assert(event.kind == EXP3_PWM_CONFIRM);
    /* Receipt alone must never publish an Accepted result. */
    assert(!g_exp3_hil.link.tx_active);
    exp3_board_complete_pwm_event(&event, false);
    for (; now<19000U && !hmi.matches; now+=4U) {
        fake_now=now;
        exp3_board_poll(now);
        if (now%120U==0U) exp3_hil_receive(&hmi, NULL, g_exp3_downlink_word, now);
    }
    assert(hmi.matches == 1U && hmi.last_result.status != EXP3_ACCEPTED);
    assert(!exp3_board_take_pwm_event(&event));
    puts("PASS controller decision required before reply; rejected Confirm is not Accepted");
}
static void accepted_controller_and_duplicate(void)
{
    Exp3Hil hmi = {0};
    Exp3Mailbox box = {0};
    BladeManualControl_t blade;
    BladeManualControlInput_t input = {0};
    Exp3PwmEvent event;
    uint32_t now;
    unsigned confirmations=0U;
    BladeManualControl_Init(&blade, NULL);
    input.link_fresh=true;
    input.remote_run_enable=true;
    input.run_permitted=true;
    input.selected_owner=EXECUTION_OWNER_MANUAL_REMOTE;
    input.ch4_raw=1500U;
    input.ch7_raw=2000U;
    input.confirmed_gear=REMOTE_BLADE_GEAR_LOW;
    input.frame_sequence=1U;
    BladeManualControl_Step(&blade, &input);
    exp3_board_channels(20000U, 1500U, 1500U, 1500U);
    exp3_board_update_intent(MOWER_AUTH_CENTER,false,2U,20000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,false,2U,20004U);
    exp3_hil_bind_session(&hmi, NULL, g_exp3_hil.controller, g_exp3_hil.hmi, 20000U);
    hmi.link.transfer=1U; /* Previous request used transfer 1 in this same session. */
    box.message=(Exp3Message){.controller=hmi.controller, .hmi=hmi.hmi,
        .request=2U, .action=2U, .generation=2U, .context=EXP3_PWM_CONTEXT,
        .target=1U, .kind=EXP3_CONFIRM, .sender=1U};
    box.command=EXP3_SEND;
    for (now=20004U; now<27000U && !hmi.matches; now+=4U) {
        uint16_t a=1500U, b=1500U;
        fake_now=now;
        input.ch4_raw=988U;
        ++input.frame_sequence;
        BladeManualControl_Step(&blade, &input);
        exp3_hil_poll(&hmi, &box, NULL, now, 20U);
        exp3_channels(hmi.published_word, &a, &b);
        exp3_board_channels(now, input.ch4_raw, a, b);
        if (exp3_board_take_pwm_event(&event)) {
            Exp3BladeInput bridge={0};
            Exp3BladeDecision decision;
            BladeConfirmOK_t ok={0};
            Exp3PwmEvent old;
            bridge.event=event;
            bridge.blade_state=BladeManualControl_GetState(&blade);
            bridge.request_sequence=BladeManualControl_GetRequestSequence(&blade);
            bridge.ch4_raw=input.ch4_raw;
            bridge.ch4_epoch=exp3_board_ch4_epoch();
            bridge.gate_permitted=true;
            bridge.baseline_event_sequence=blade.baseline_event_sequence;
            bridge.last_consumed_event_sequence=blade.last_consumed_event_sequence;
            decision=exp3_blade_adapter_decide(&bridge);
            assert(decision.inject_confirm);
            old=event;
            old.message.action=1U;
            exp3_board_complete_pwm_event(&old, true);
            exp3_board_poll(now);
            assert(!g_exp3_hil.link.tx_active); /* wrong/old completion cannot reply */
            ok.version=BLADE_PROTOCOL_VERSION;
            ok.request_sequence=decision.request_sequence;
            ok.event_sequence=decision.event_sequence;
            BladeManualControl_InjectConfirmOK(&blade, &ok);
            BladeManualControl_Step(&blade, &input);
            assert(BladeManualControl_GetState(&blade)==BLADE_STATE_RUNNING);
            assert(BladeManualControl_GetCommand(&blade)==0.0f);
            BladeManualControl_Step(&blade, &input);
            assert(BladeManualControl_GetCommand(&blade)==0.0f); /* same frame */
            exp3_board_complete_pwm_event(&event, true);
            ++confirmations;
        }
        exp3_board_poll(now);
        if(now%120U==0U) exp3_hil_receive(&hmi,NULL,g_exp3_downlink_word,now);
    }
    assert(hmi.matches==1U && hmi.last_result.status==EXP3_ACCEPTED);
    assert(confirmations==1U && BladeManualControl_GetCommand(&blade)==blade.config.output_min);
    input.ch7_raw=1500U; input.confirmed_gear=REMOTE_BLADE_GEAR_MEDIUM;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==blade.config.output_medium);
    input.ch7_raw=1000U; input.confirmed_gear=REMOTE_BLADE_GEAR_HIGH;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==blade.config.output_max);
    input.emergency_active=true; input.physical_latched=true;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==0.0f);
    input.emergency_active=false; input.physical_latched=false;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==0.0f);
    box.command=EXP3_SEND; /* retransmit same action after safety stop */
    for (now=28000U; now<35000U && hmi.matches<2U; now+=4U) {
        uint16_t a,b;
        fake_now=now;
        exp3_hil_poll(&hmi,&box,NULL,now,20U);
        exp3_channels(hmi.published_word,&a,&b);
        exp3_board_channels(now,988U,a,b);
        assert(!exp3_board_take_pwm_event(&event));
        exp3_board_poll(now);
        if(now%120U==0U) exp3_hil_receive(&hmi,NULL,g_exp3_downlink_word,now);
    }
    assert(hmi.matches==2U && confirmations==1U);
    assert(BladeManualControl_GetCommand(&blade)==0.0f);
    unsigned cancels=0U;
    box.message.kind=EXP3_CANCEL; box.command=EXP3_SEND;
    for (now=35000U; now<55000U && hmi.matches<3U; now+=4U) {
        uint16_t a,b;
        fake_now=now; input.ch4_raw=1500U;
        ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
        exp3_hil_poll(&hmi,&box,NULL,now,20U);
        exp3_channels(hmi.published_word,&a,&b);
        exp3_board_channels(now,1500U,a,b);
        if (exp3_board_take_pwm_event(&event)) {
            assert(event.kind==EXP3_PWM_CANCEL && event.message.action==2U);
            ++cancels;
            exp3_board_complete_pwm_event(&event,true);
        }
        exp3_board_poll(now);
        // Drop the entire first Cancel receipt; retry must replay cached result,
        // not inject a second blade event or create a fresh authorization.
        if(now>=43000U && now%120U==0U)
            exp3_hil_receive(&hmi,NULL,g_exp3_downlink_word,now);
        assert(BladeManualControl_GetCommand(&blade)==0.0f);
    }
    assert(hmi.matches==3U && hmi.last_result.status==EXP3_CANCELLED);
    assert(hmi.cancel_retries==1U && cancels==1U && confirmations==1U);

    printf("PASS full controller Confirm/Result, fresh-frame low/medium/high, safety zero, duplicate no reauthorization (driver=%u)\n",
           (unsigned)BLADE_MOTOR_DRIVER_USED);
}

static void verified_unlock_and_duplicate(void)
{
    Exp3Hil hmi = {0};
    Exp3Mailbox box = {0};
    Exp3PwmEvent event;
    RemoteAuthorization_t auth;
    RemoteAuthorizationInput_t input = {0};
    FlySkyIbusConfig_t config = {0};
    unsigned confirms = 0U;
    uint32_t now;
    config.run_enable_semantics_configured = true;
    config.run_enable_on_us = 2000U; config.run_enable_off_us = 1000U;
    config.switch_position_tolerance_us = 100U; config.switch_confirm_frames = 3U;
    config.steering.center_us = config.throttle.center_us = 1500U;
    config.stick_center_tolerance_us = 50U; config.stick_center_hold_ms = 200U;
    RemoteAuthorization_Init(&auth, &config);
    input.link_fresh = input.stable_online = true;
    input.steering_raw = input.throttle_raw = input.ch4_raw = 1500U;
    input.run_enable_raw = 2000U;
    for (now = 60000U; now <= 60400U; now += 10U) {
        ++input.frame_sequence;
        RemoteAuthorization_Advance(&auth, &input, now);
        exp3_board_channels(now, input.ch4_raw, 1500U, 1500U);
    }
    exp3_hil_bind_session(&hmi, NULL, g_exp3_hil.controller, g_exp3_hil.hmi, now);
    hmi.link.transfer = 20U;
    now = (now + 3U) & ~3U;
    box.message = (Exp3Message){.controller=hmi.controller,.hmi=hmi.hmi,
        .request=3U,.action=3U,.generation=3U,.context=EXP3_UNLOCK_CONTEXT,
        .target=EXP3_UNLOCK_TARGET,.kind=EXP3_CONFIRM,.sender=1U};
    box.command = EXP3_SEND;
    /* Two earlier left edges belonged to blade tests, but both generation
     * streams still observe them. This unlock is the third physical edge. */
    assert(exp3_board_unlock_epoch() == 2U);
    exp3_board_update_intent(MOWER_AUTH_CENTER,true,3U,now);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,3U,now);
    uint32_t confirm_started = now;
    for (; now < 71000U && !hmi.matches; now += 4U) {
        uint16_t a, b;
        fake_now = now; input.ch4_raw = 1000U; ++input.frame_sequence;
        RemoteAuthorization_Advance(&auth, &input, now);
        exp3_hil_poll(&hmi, &box, NULL, now, 20U);
        exp3_channels(hmi.published_word, &a, &b);
        exp3_board_channels(now, 1000U, a, b);
        if (exp3_board_take_pwm_event(&event)) {
            assert(now - confirm_started <= 500U);
            printf("MEASURE unlock controller acceptance=%ums (120ms downlink sampling model)\n", (unsigned)(now-confirm_started));
            assert(event.message.context == EXP3_UNLOCK_CONTEXT);
            assert(event.message.generation == exp3_board_unlock_epoch());
            assert(RemoteAuthorization_ConfirmOK(&auth, &input, now));
            assert(!auth.output.run_permitted);
            exp3_board_complete_pwm_event(&event, true); ++confirms;
        }
        exp3_board_poll(now);
        if (now%120U == 0U) exp3_hil_receive(&hmi, NULL, g_exp3_downlink_word, now);
    }
    assert(hmi.matches == 1U && hmi.last_result.status == EXP3_ACCEPTED);
    assert(auth.output.run_permitted && confirms == 1U);
    RemoteAuthorization_Revoke(&auth);
    box.command = EXP3_SEND;
    for (; now < 81000U && hmi.matches < 2U; now += 4U) {
        uint16_t a, b;
        fake_now = now; ++input.frame_sequence;
        RemoteAuthorization_Advance(&auth, &input, now);
        exp3_hil_poll(&hmi, &box, NULL, now, 20U);
        exp3_channels(hmi.published_word, &a, &b);
        exp3_board_channels(now, 1000U, a, b);
        assert(!exp3_board_take_pwm_event(&event));
        exp3_board_poll(now);
        if (now%120U == 0U) exp3_hil_receive(&hmi, NULL, g_exp3_downlink_word, now);
        assert(!auth.output.run_permitted);
    }
    assert(hmi.matches == 2U && confirms == 1U);
    puts("PASS unlock Confirm/real authorization/compact Result, separate blade generation, cached duplicate cannot unlock again");
}

static void token_boundaries(void)
{
    uint32_t controller=g_exp3_hil.controller+1U;
    exp3_hil_bind_session(&g_exp3_hil,NULL,controller,42U,80000U);
    assert(!g_exp3_hil.token);
    exp3_board_update_intent(MOWER_AUTH_CENTER,true,1U,80000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,1U,80010U);
    Exp3Message m={.controller=controller,.hmi=42U,.generation=1U,
      .context=EXP3_UNLOCK_CONTEXT,.target=1U};
    assert(exp3_board_check_intent(&m,90009U)); /* 9999ms */
    assert(!exp3_board_check_intent(&m,90009U)); /* consumed exactly once */
    exp3_board_update_intent(MOWER_AUTH_CENTER,true,2U,100000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,2U,100010U);
    m.generation=2U;assert(!exp3_board_check_intent(&m,110010U)); /* exact 10000 */
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,2U,110020U);
    assert(!g_exp3_hil.token_active && g_exp3_hil.token==2U); /* no held-left renewal */
    exp3_board_update_intent(MOWER_AUTH_CENTER,false,3U,0xfffff000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,false,3U,0xfffff001U);
    m.context=EXP3_PWM_CONTEXT;m.generation=3U;
    assert(exp3_board_check_intent(&m,(uint32_t)(0xfffff001U+9999U)));
    g_exp3_hil.token=4094U;
    exp3_board_update_intent(MOWER_AUTH_CENTER,true,4U,120000U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,4U,120010U);
    assert(g_exp3_hil.bound && g_exp3_hil.token==4095U);
    exp3_board_update_intent(MOWER_AUTH_CENTER,true,5U,120020U);
    exp3_board_update_intent(MOWER_AUTH_CONFIRM,true,5U,120030U);
    assert(!g_exp3_hil.bound && g_exp3_hil.restart_requested);
    exp3_board_poll(120030U);
    Exp3PwmEvent event;assert(exp3_board_take_pwm_event(&event));
    assert(event.kind==EXP3_PWM_CANCEL && !event.message.kind);
    puts("PASS actual token authority: 9999/10000ms, single consumption, held-left expiry, clock wrap, 4095 exhaustion revokes and queues fresh session");
}

int main(void)
{
    Exp3AutoSession hmi;
    Exp3PwmEvent event;
    exp3_auto_init(&hmi, EXP3_AUTO_HMI, 0x11112222U, 0U);
    run_pair(&hmi, 0U, 10000U);
    assert(g_exp3_hil.bound);
    assert(exp3_board_take_pwm_event(&event) && event.kind == EXP3_PWM_CANCEL);
    exp3_board_complete_pwm_event(&event, false);
    verified_controller_reply();
    accepted_controller_and_duplicate();
    verified_unlock_and_duplicate();
    token_boundaries();
    return 0;
}
