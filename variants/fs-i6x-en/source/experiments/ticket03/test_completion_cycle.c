#include "exp3_board.h"
#include "exp3_blade_adapter.h"
#include "blade_manual_control.h"
#include "sens_ibus_responder.h"
#include "main.h"
#include "battery_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern Exp3Hil controller_g_exp3_hil;
extern volatile Exp3Mailbox controller_g_exp3_mailbox;
void controller_exp3_board_channels(uint32_t, uint16_t, uint16_t, uint16_t);
void controller_exp3_board_poll(uint32_t);
bool controller_exp3_board_take_pwm_event(Exp3PwmEvent *);
void controller_exp3_board_complete_pwm_event(const Exp3PwmEvent *, bool);
uint32_t controller_exp3_board_ch4_epoch(void);
uint32_t controller_exp3_board_unlock_epoch(void);
uint32_t controller_exp3_board_completion_part(unsigned);
uint16_t controller_exp3_board_update_intent(unsigned,bool,uint32_t,uint32_t);

static uint32_t now, action, frame, executed_at;
static unsigned worst_execution;
static unsigned phase, accepted_starts, motion_events, slow_overlap, battery_words;
static uint16_t ch4=1500U;
static bool unlocked, fault, drop_fast, drop_slow;
static BladeManualControl_t blade;
static SensIbusResponder_t sensors;
static uint8_t response[8], response_size;
static struct { uint32_t due; uint8_t packet[37]; bool ready; } rf;
struct FakeSysTick fake_systick={17U};
uint32_t HAL_GetTick(void) { return now; }
uint32_t HAL_GetUIDw0(void) { return 0x12345678U; }
uint32_t HAL_GetUIDw1(void) { return 0x9abcdef0U; }
uint32_t TelemetrySnapshot_ReadFastAuthorization(void) {
    uint8_t output=BladeManualControl_GetCommand(&blade)>0.0f?3U:0U;
    return mower_fast_status_pack(output,unlocked?0U:0x400U,unlocked&&!fault,now) |
        ((uint32_t)controller_g_exp3_hil.authority<<16);
}
static bool send_response(void *unused,const uint8_t *bytes,uint8_t count) {
    (void)unused; assert(count<=sizeof(response));
    memcpy(response,bytes,count); response_size=count; return true;
}
static bool read_snapshot(void *unused,TelemetrySnapshot_t *snapshot) {
    (void)unused; memset(snapshot,0,sizeof(*snapshot));
    snapshot->generated_at_ms=now;
    snapshot->values[TELEMETRY_VALUE_BATTERY_MIN_CELL]=3350U;
    return true;
}
static void request(uint8_t command) {
    uint8_t bytes[4]={4U,command,(uint8_t)(0xfbU-command),0xffU};
    response_size=0U; SensIbusResponder_Feed(&sensors,bytes,4U);
    if(response_size) {
        unsigned sum=0xffffU;
        for(unsigned i=0;i<response_size-2U;i++) sum-=response[i];
        assert(response[response_size-2U]==(uint8_t)sum);
        assert(response[response_size-1U]==(uint8_t)(sum>>8));
    }
}
static void motion_tick(void) {
    BladeManualControlInput_t input={0};
    input.link_fresh=true; input.remote_run_enable=true;
    input.run_permitted=unlocked&&!fault; input.software_locked=!unlocked;
    input.physical_latched=fault; input.selected_owner=EXECUTION_OWNER_MANUAL_REMOTE;
    input.ch4_raw=ch4; input.ch7_raw=2000U;
    input.confirmed_gear=REMOTE_BLADE_GEAR_LOW; input.frame_sequence=frame;
    unsigned auth_phase=fault?MOWER_AUTH_SAFETY:ch4==1500U?MOWER_AUTH_CENTER:
        (!unlocked || blade.state==BLADE_STATE_START_CONFIRM_PENDING)?MOWER_AUTH_CONFIRM:MOWER_AUTH_CH4;
    controller_exp3_board_update_intent(auth_phase,!unlocked,unlocked?blade.request_sequence:0U,now);
    Exp3PwmEvent event={0}; bool ready=controller_exp3_board_take_pwm_event(&event);
    bool injected=false;
    if(ready) {
        ++motion_events;
        if(event.message.context==EXP3_UNLOCK_CONTEXT) {
            bool accepted=!fault && event.message.generation==controller_exp3_board_unlock_epoch();
            if(event.kind==EXP3_PWM_CONFIRM && accepted) { unlocked=true; executed_at=now; }
            controller_exp3_board_complete_pwm_event(&event,accepted);
        } else {
            Exp3BladeInput bridge={0};
            bridge.event=event; bridge.blade_state=blade.state;
            bridge.ch4_raw=ch4; bridge.ch4_epoch=controller_exp3_board_ch4_epoch();
            bridge.request_sequence=blade.request_sequence;
            bridge.baseline_event_sequence=blade.baseline_event_sequence;
            bridge.last_consumed_event_sequence=blade.last_consumed_event_sequence;
            bridge.gate_permitted=unlocked&&!fault;
            Exp3BladeDecision decision=exp3_blade_adapter_decide(&bridge);
            if(decision.inject_confirm) {
                BladeConfirmOK_t ok={.version=BLADE_PROTOCOL_VERSION,
                    .request_sequence=decision.request_sequence,.event_sequence=decision.event_sequence};
                BladeManualControl_InjectConfirmOK(&blade,&ok); injected=true;
            }
            input.command_invalidated=decision.invalidate;
        }
    }
    BladeManualControl_Step(&blade,&input);
    if(ready && event.message.context==EXP3_PWM_CONTEXT) {
        bool accepted=event.kind==EXP3_PWM_CANCEL || (injected && blade.state==BLADE_STATE_RUNNING);
        controller_exp3_board_complete_pwm_event(&event,accepted);
        if(injected && accepted) { ++accepted_starts; executed_at=now; }
    }
    if(fault || !unlocked || ch4==1500U) assert(BladeManualControl_GetCommand(&blade)==0.0f);
}
static void tick(void) {
    ++now;
    exp3_board_poll(now);
    if(now%5U==0U) {
        uint16_t a=1500U,b=1500U;
        if(g_exp3_uplink_channels) { a=(uint16_t)g_exp3_uplink_channels; b=(uint16_t)(g_exp3_uplink_channels>>16); }
        ++frame; controller_exp3_board_channels(now,ch4,a,b);
    }
    if(now%10U==0U) motion_tick();
    if(now%20U==0U) controller_exp3_board_poll(now);
    if((now+phase)%10U==0U) {
        unsigned address=1U+((now+phase)/10U)%15U;
        request((uint8_t)(0xa0U+address));
        if(response_size && address>=9U && !(drop_slow && address==9U) &&
           !(drop_fast && address>=13U)) {
            uint16_t word=(uint16_t)(response[2]|(response[3]<<8));
            if(address==9U && ((word&MOWER_BATTERY_DIAG_KIND)==MOWER_BATTERY_DIAG_MIN ||
                (word&MOWER_BATTERY_DIAG_KIND)==MOWER_BATTERY_DIAG_REASON)) ++battery_words;
            assert(!rf.ready);
            memset(rf.packet,0xff,sizeof(rf.packet));
            rf.packet[0]=response_size==8U?0xacU:0xaaU;
            rf.packet[9]=address==9U?0xe1U:address==10U?MOWER_FAST_STATUS_TYPE:
                address==11U?0xe3U:address==12U?0xe4U:(uint8_t)(EXP3_COMPLETION_TYPE+address-13U);
            rf.packet[10]=(uint8_t)address;
            unsigned offset=11U;
            if(response_size==8U) rf.packet[offset++]=4U;
            memcpy(rf.packet+offset,response+2U,response_size-4U);
            rf.due=now+5U; rf.ready=true;
        }
    }
    if(rf.ready && now==rf.due) {
        exp3_board_capture_rf(rf.packet,37U,now); rf.ready=false;
        exp3_board_poll(now);
    }
}
static void advance(unsigned ms) { while(ms--) tick(); }
static Exp3Message command(unsigned kind,unsigned context,uint32_t id,uint32_t generation) {
    return (Exp3Message){.controller=g_exp3_hil.controller,.hmi=g_exp3_hil.hmi,
        .request=id,.action=id,.generation=generation,.context=context,.target=1U,
        .kind=kind,.sender=1U};
}
static unsigned transact(const Exp3Message *m,unsigned limit) {
    uint32_t started=now, matches=g_exp3_hil.matches;
    assert(exp3_board_submit(m));
    while(g_exp3_hil.matches==matches && now-started<limit) tick();
    if(g_exp3_hil.matches!=matches+1U) {
        fprintf(stderr,"FAIL timeout kind=%u context=%08x action=%u time=%u tx=%u slot=%u\n",
            m->kind,(unsigned)m->context,(unsigned)m->action,(unsigned)(now-started),g_exp3_hil.link.tx_active,
            controller_g_exp3_hil.link.tx_index); assert(0);
    }
    assert(g_exp3_hil.last_result.status==(m->kind==EXP3_CANCEL?EXP3_CANCELLED:EXP3_ACCEPTED));
    if(controller_g_exp3_hil.link.tx_active) ++slow_overlap;
    if (m->kind==EXP3_CONFIRM) {
        assert(executed_at>=started && executed_at-started<=250U);
        if(executed_at-started>worst_execution) worst_execution=executed_at-started;
    }
    advance(1U); // The final transmit word must still finish before a new command.
    return now-started;
}
static unsigned blade_cycle(unsigned unlock_latency) {
    ch4=1000U; advance(30U);
    Exp3Message start=command(EXP3_CONFIRM,EXP3_PWM_CONTEXT,++action,controller_exp3_board_ch4_epoch());
    unsigned latency=transact(&start,700U);
    assert(latency<=700U && latency<=unlock_latency+150U);
    advance(20U); assert(blade.state==BLADE_STATE_RUNNING && BladeManualControl_GetCommand(&blade)>0.0f);
    ch4=1500U; advance(30U);
    assert(BladeManualControl_GetCommand(&blade)==0.0f);
    Exp3Message stop=start; stop.kind=EXP3_CANCEL;
    unsigned stopped=transact(&stop,700U);
    assert(stopped<=700U && stopped<=unlock_latency+150U);
    advance(20U);
    return latency>stopped?latency:stopped;
}
int main(void) {
    controller_g_exp3_mailbox.message=(Exp3Message){.controller=0x12345678U,.hmi=0xabcdef01U};
    controller_g_exp3_mailbox.command=EXP3_ARM;
    controller_exp3_board_poll(0U);
    assert(controller_g_exp3_hil.bound);
    exp3_hil_bind_session(&g_exp3_hil,NULL,controller_g_exp3_hil.controller,controller_g_exp3_hil.hmi,0U);
    SensIbusResponder_Init(&sensors,read_snapshot,NULL,send_response,NULL);
    BladeManualControl_Init(&blade,NULL);
    for(unsigned address=13U;address<=15U;address++) {
        request((uint8_t)(0x80U+address)); assert(response_size==4U);
        request((uint8_t)(0x90U+address));
        assert(response_size==6U && response[2]==EXP3_COMPLETION_TYPE+address-13U && response[3]==4U);
        request((uint8_t)(0xa0U+address)); assert(response_size==8U);
    }
    advance(250U);
    unsigned worst=0U, baseline=0U, cycles=0U;
    for(phase=0U;phase<150U;phase++) {
        unlocked=false; ch4=1500U; advance(220U);
        ch4=1000U; advance(30U);
        Exp3Message unlock=command(EXP3_CONFIRM,EXP3_UNLOCK_CONTEXT,++action,controller_exp3_board_unlock_epoch());
        unsigned latency=transact(&unlock,700U);
        assert(unlocked && blade.state!=BLADE_STATE_RUNNING);
        if(latency>baseline) baseline=latency;
        ch4=1500U; advance(30U);
        Exp3Message cancel=unlock; cancel.kind=EXP3_CANCEL; transact(&cancel,700U);
        unsigned cycle=blade_cycle(latency); ++cycles;
        if(cycle>worst) worst=cycle;
    }
    phase=73U;
    for(unsigned i=0U;i<100U;i++) {
        unsigned cycle=blade_cycle(baseline); ++cycles;
        if(cycle>worst) worst=cycle;
    }
    printf("MEASURE actual Motion execution <=%ums (target 250ms)\n",worst_execution);
    assert(baseline<=400U && worst<=400U);
    assert(accepted_starts==cycles && slow_overlap>100U && battery_words>0U);
    printf("MEASURE real adapters/SENS/RF/blade, 15 slots / 150ms cycle + 5ms RF: unlock<=%ums, start/stop/restart<=%ums, 150 phases + 100 continuous cycles, driver=%u\n",
        baseline,worst,(unsigned)BLADE_MOTOR_DRIVER_USED);
    // New radio + old two-byte-only receiver uses the unchanged full receipt.
    drop_fast=true;
    ch4=1000U; advance(30U);
    Exp3Message legacy=command(EXP3_CONFIRM,EXP3_PWM_CONTEXT,++action,controller_exp3_board_ch4_epoch());
    unsigned legacy_latency=transact(&legacy,11000U);
    assert(legacy_latency>700U && blade.state==BLADE_STATE_RUNNING);
    ch4=1500U; advance(30U); legacy.kind=EXP3_CANCEL; transact(&legacy,11000U);
    assert(BladeManualControl_GetCommand(&blade)==0.0f);
    puts("PASS idle battery diagnostics/slow-frame overlap do not block Motion, no duplicate starts, matching Cancel, old-receiver legacy fallback");
}
