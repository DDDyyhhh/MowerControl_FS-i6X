#include "exp3_board.h"
#include "main.h"
#include "exp3_blade_adapter.h"
#include "mower_mode_control.h"
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
    assert(confirmations==1U && BladeManualControl_GetCommand(&blade)==45.0f);
    input.ch7_raw=1500U; input.confirmed_gear=REMOTE_BLADE_GEAR_MEDIUM;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==50.0f);
    input.ch7_raw=1000U; input.confirmed_gear=REMOTE_BLADE_GEAR_HIGH;
    ++input.frame_sequence; BladeManualControl_Step(&blade,&input);
    assert(BladeManualControl_GetCommand(&blade)==55.0f);
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

    puts("PASS full controller Confirm/Result, fresh-frame PWM 45/50/55, safety zero, duplicate no reauthorization");
}

static void chassis_confirmation_flow(void)
{
    MowerModeControl_t mode;
    MowerModeInput_t input={true,true,false,true,true,1000U,0U};
    Exp3Hil hmi={0}; Exp3Mailbox box={0};
    uint32_t now=60000U;
    MowerMode_Init(&mode);
    MowerMode_Session(&mode,g_exp3_hil.controller,g_exp3_hil.hmi);
    for(unsigned i=0;i<30;++i) { ++input.frame_sequence; now+=10U; MowerMode_Advance(&mode,&input,now); }
    assert(mode.ready);
    exp3_hil_bind_session(&hmi,NULL,g_exp3_hil.controller,g_exp3_hil.hmi,now);
    hmi.link.transfer=10U;
    box.message=(Exp3Message){.controller=hmi.controller,.hmi=hmi.hmi,.request=3U,.action=3U,
        .generation=mode.generation,.context=MOWER_CHASSIS_CONTEXT,.target=MOWER_CHASSIS_TARGET,
        .kind=EXP3_CONFIRM,.sender=1U};
    const uint16_t generation=mode.generation;
    unsigned confirms=0U,cancels=0U;
    for(unsigned transaction=0;transaction<4U;++transaction) {
        if (transaction==1U) box.message.kind=EXP3_CANCEL;
        if (transaction==2U) { box.message.kind=EXP3_CONFIRM; box.message.action=box.message.request=4U; }
        if (transaction==3U) { box.message.action=box.message.request=5U; box.message.generation=mode.generation; }
        box.command=EXP3_SEND;
        uint32_t deadline=now+20000U;
        while(now<deadline && hmi.matches<=transaction) {
            uint16_t a,b; Exp3PwmEvent event;
            now+=4U; fake_now=now;
            ++input.frame_sequence; MowerMode_Advance(&mode,&input,now);
            exp3_hil_poll(&hmi,&box,NULL,now,20U);
            exp3_channels(hmi.published_word,&a,&b);
            exp3_board_channels(now,1500U,a,b);
            assert(!exp3_board_take_pwm_event(&event)); /* CHAS cannot reach blade execution. */
            if (exp3_board_take_chassis_event(&event)) {
                bool accepted;
                if (event.kind==EXP3_PWM_CONFIRM) {
                    accepted=MowerMode_Confirm(&mode,event.message.generation);
                    if (accepted) ++confirms;
                } else { (void)MowerMode_Cancel(&mode,event.message.generation); ++cancels; accepted=true; }
                exp3_board_complete_pwm_event(&event,accepted);
            }
            exp3_board_poll(now);
            if (now%120U==0U) exp3_hil_receive(&hmi,NULL,g_exp3_downlink_word,now);
        }
        assert(hmi.matches==transaction+1U);
        if (transaction==0U || transaction==3U) assert(mode.armed && hmi.last_result.status==EXP3_ACCEPTED);
        if (transaction==1U) assert(!mode.armed && hmi.last_result.status==EXP3_CANCELLED && mode.generation!=generation);
        if (transaction==2U) assert(!mode.armed && hmi.last_result.status!=EXP3_ACCEPTED);
    }
    assert(confirms==2U && cancels==1U);
    puts("PASS end-to-end CHAS Confirm/Cancel/Result, expired generation and independent blade dispatch");
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
    chassis_confirmation_flow();
    uint32_t old_controller=g_exp3_hil.controller;
    exp3_board_request_rebind();
    run_pair(&hmi,200000U,210000U);
    assert(g_exp3_hil.bound && g_exp3_hil.controller!=old_controller);
    assert(g_mower_unlock_generation==0U && g_mower_control_word==0U);
    assert(!exp3_board_take_chassis_event(&event));
    puts("PASS controller request rotates session and clears pending chassis authorization");
    return 0;
}
