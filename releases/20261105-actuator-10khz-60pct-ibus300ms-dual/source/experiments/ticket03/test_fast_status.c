#include "exp3_board.h"
#include "telemetry_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void produce_faults(uint32_t now, uint16_t faults)
{
    TelemetrySources_t s = {0};
    s.system_locked = (faults & 0x400U) != 0U;
    s.emergency_active = (faults & 1U) != 0U;
    s.collision_active_or_latched = (faults & 2U) != 0U;
    s.manual_remote_available = true;
    s.final_manual_run_permitted = faults == 0U;
    s.bms.valid = true; s.bms.voltage_centivolts = 4800; s.bms.soc_percent = 80;
    s.bms.last_good_ms = now;
    for (unsigned i=0;i<4;i++) { s.wheels[i].valid=true; s.wheels[i].last_good_ms=now; }
    TelemetrySnapshot_Update(&s,now);
}
static void produce(uint32_t now, bool locked)
{
    produce_faults(now, locked ? 0x400U : 0U);
}
static void packet(uint16_t word, uint32_t now, uint8_t type)
{
    uint8_t p[37];memset(p,0xff,sizeof(p));
    p[0]=0xac;p[9]=type;p[10]=10U;p[11]=4U;
    p[12]=(uint8_t)word;p[13]=(uint8_t)(word>>8);p[14]=0U;p[15]=0U;
    exp3_board_rf(p,sizeof(p),now);
}
static void packet_ac(uint16_t word, uint32_t now)
{
    uint8_t p[37]; memset(p,0xff,sizeof(p));
    p[0]=0xac;p[9]=MOWER_FAST_STATUS_TYPE;p[10]=10U;p[11]=4U;
    p[12]=(uint8_t)word;p[13]=(uint8_t)(word>>8);
    p[14]=0U;p[15]=0U;
    exp3_board_rf(p,sizeof(p),now);
}
int main(void)
{
    uint16_t word;
    assert(MOWER_FAST_STATUS_TYPE == 0xE9U);
    word=mower_fast_status_pack(7U,1U,1U,0U);
    assert((word & MOWER_FAST_EMERGENCY) && (word & MOWER_FAST_LOCKED));
    assert(!(word & (MOWER_FAST_RUN | 7U))); /* Emergency overrides any output request. */
    // Preserve raw expiry and the ACT validity bit while silencing only its alarm.
    TelemetrySources_t stale = {0};
    stale.manual_remote_available=true; stale.final_manual_run_permitted=true;
    stale.bms.valid=true; stale.bms.last_good_ms=1000U;
    stale.bms.voltage_centivolts=4800U;
    for(unsigned i=0;i<4;i++) { stale.wheels[i].valid=true; stale.wheels[i].last_good_ms=1000U; }
    stale.wheels[0].last_good_ms=849U;
    TelemetrySnapshot_Reset(); TelemetrySnapshot_Update(&stale,1000U);
    TelemetrySnapshot_t snapshot;
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT]==TELEMETRY_FAULT_WHEEL_STALE);
    word=TelemetrySnapshot_ReadFastStatus();
    assert(word&MOWER_FAST_WHEEL_STALE);
    assert(!(word&MOWER_FAST_FAULT));
    stale.bms.valid=false; TelemetrySnapshot_Update(&stale,1100U);
    assert(TelemetrySnapshot_Read(&snapshot));
    assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT]&0x40U);
    word=TelemetrySnapshot_ReadFastStatus();
    assert(!(word&MOWER_FAST_FAULT) && (word&MOWER_FAST_RUN));
    assert(mower_fast_status_pack(0U,4U,0U,0U)&MOWER_FAST_FAULT); /* Tilt still alarms. */
    g_exp3_hil.bound=1;g_exp3_hil.controller=1;g_exp3_hil.hmi=2;
    packet(0x0028,1000,0xe2);packet(0x0128,1120,0xe2);
    assert(!exp3_board_runtime(1120,&word));
    packet(0x0028,1200,0xe5);packet(0x0228,1320,0xe5);
    assert(!exp3_board_runtime(1320,&word)); /* old profile cannot establish profile-3 authority. */
    uint8_t legacy[37];memset(legacy,0xff,sizeof(legacy));
    legacy[0]=0xaa;legacy[9]=MOWER_FAST_STATUS_TYPE;legacy[10]=10U;legacy[11]=0x28;legacy[12]=0;
    exp3_board_rf(legacy,sizeof(legacy),2000U);legacy[12]=2U;
    exp3_board_rf(legacy,sizeof(legacy),2100U);assert(!exp3_board_runtime(2100U,&word));
    legacy[0]=0xac;legacy[11]=2U;legacy[12]=0x28;legacy[13]=4U;
    exp3_board_rf(legacy,sizeof(legacy),2200U);assert(!exp3_board_runtime(2200U,&word));
    legacy[11]=4U;exp3_board_rf(legacy,15U,2300U);assert(!exp3_board_runtime(2300U,&word));
    unsigned worst=0;
    for (unsigned phase=0; phase<120U; phase++) {
        uint32_t start=10000U+phase*3000U;
        unsigned observed=0;
        TelemetrySnapshot_Reset();
        for (unsigned t=0;t<1000U;t++) {
            if(t%10U==0U) produce(start+t,t<500U);
            if((t+phase)%120U==0U) packet(TelemetrySnapshot_ReadFastStatus(),start+t,MOWER_FAST_STATUS_TYPE);
            if(t>=500U && !observed && exp3_board_runtime(start+t,&word) && !(word&MOWER_FAST_LOCKED)) {
                assert(word&MOWER_FAST_RUN); observed=t-500U+1U;
            }
        }
        assert(observed && observed-1U<=300U);
        if(observed-1U>worst) worst=observed-1U;
    }
    unsigned worst_emergency=0;
    const uint16_t transitions[][2]={{0U,0x401U},{0x400U,0x401U},
        {0x401U,0x403U},{0x403U,0x400U},{0x401U,0U}};
    for (unsigned scenario=0;scenario<sizeof(transitions)/sizeof(transitions[0]);++scenario) {
        for (unsigned phase=0;phase<120U;++phase) {
            uint32_t start=4000000U+scenario*400000U+phase*3000U;
            unsigned observed=0;
            TelemetrySnapshot_Reset();
            uint16_t expected=mower_fast_status_pack(0U,transitions[scenario][1],
                transitions[scenario][1]==0U,0U);
            for (unsigned t=0;t<1000U;++t) {
                if(t%10U==0U) produce_faults(start+t,transitions[scenario][t>=500U]);
                if((t+phase)%120U==0U) {
                    uint16_t sample=TelemetrySnapshot_ReadFastStatus();
                    if(phase&1U) packet_ac(sample,start+t);
                    else packet(sample,start+t,MOWER_FAST_STATUS_TYPE);
                }
                if(t>=500U && !observed && exp3_board_runtime(start+t,&word) &&
                   (word & (MOWER_FAST_STOP_MASK|MOWER_FAST_RUN))==
                   (expected & (MOWER_FAST_STOP_MASK|MOWER_FAST_RUN))) observed=t-500U+1U;
            }
            assert(observed && observed-1U<=300U);
            if(scenario<2U && observed-1U>worst_emergency) worst_emergency=observed-1U;
        }
    }
    TelemetrySnapshot_Reset();produce(500000U,false);
    packet(TelemetrySnapshot_ReadFastStatus(),500000U,MOWER_FAST_STATUS_TYPE);
    produce(500100U,false);packet(TelemetrySnapshot_ReadFastStatus(),500100U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(500100U,&word) && (word&MOWER_FAST_RUN));
    uint16_t stopped=(uint16_t)((word&MOWER_FAST_HEARTBEAT_WORD_MASK)|MOWER_FAST_VALID|
        MOWER_FAST_LOCKED|MOWER_FAST_COLLISION|MOWER_FAST_EMERGENCY|MOWER_FAST_FAULT);
    packet(stopped,500120U,MOWER_FAST_STATUS_TYPE);
    packet(word,500130U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(500130U,&word) && !(word&MOWER_FAST_RUN) && (word&MOWER_FAST_LOCKED));
    assert(word&MOWER_FAST_EMERGENCY);
    for(unsigned t=500200U;t<=501200U;t+=100U) packet(stopped,t,MOWER_FAST_STATUS_TYPE);
    assert(!exp3_board_runtime(501200U,&word));
    TelemetrySnapshot_Reset();produce(600000U,false);
    packet(TelemetrySnapshot_ReadFastStatus(),600000U,MOWER_FAST_STATUS_TYPE);
    produce(606300U,false);packet(TelemetrySnapshot_ReadFastStatus(),606300U,MOWER_FAST_STATUS_TYPE);
    assert(!exp3_board_runtime(606300U,&word));
    produce(606400U,false);packet(TelemetrySnapshot_ReadFastStatus(),606400U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(606400U,&word) &&
        ((word>>MOWER_FAST_HEARTBEAT_SHIFT)&MOWER_FAST_HEARTBEAT_MASK)==0U); /* modulo-64 wrap */
    uint16_t valid=word;
    packet((uint16_t)((63U<<MOWER_FAST_HEARTBEAT_SHIFT)|MOWER_FAST_VALID|
        MOWER_FAST_LOCKED|MOWER_FAST_EMERGENCY),606410U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(606410U,&word) && word==valid); /* reversed heartbeat */
    packet((uint16_t)((32U<<MOWER_FAST_HEARTBEAT_SHIFT)|MOWER_FAST_VALID|
        MOWER_FAST_LOCKED|MOWER_FAST_EMERGENCY),606420U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(606420U,&word) && word==valid); /* ambiguous half-cycle */
    packet((uint16_t)(valid|MOWER_FAST_LOCKED|MOWER_FAST_EMERGENCY),606430U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(606430U,&word) && word==valid); /* inconsistent stop + RUN */
    packet((uint16_t)(MOWER_FAST_VALID|MOWER_FAST_EMERGENCY),606440U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(606440U,&word) && word==valid); /* emergency requires lock */
    TelemetrySnapshot_Reset();produce(0xffffff00U,false);
    packet(TelemetrySnapshot_ReadFastStatus(),0xffffff00U,MOWER_FAST_STATUS_TYPE);
    produce(0xffffff80U,false);packet(TelemetrySnapshot_ReadFastStatus(),0xffffff80U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(0xffffff80U,&word));
    produce(0x10U,false);packet(TelemetrySnapshot_ReadFastStatus(),0x10U,MOWER_FAST_STATUS_TYPE);
    assert(exp3_board_runtime(0x10U,&word)); /* actual producer and consumer clock wrap */
    g_exp3_hil.bound=0;assert(!exp3_board_runtime(0x10U,&word));
    printf("MEASURE Motion snapshot -> real RF parser: worst unlock display state=%ums over 120 receiver phases (120ms sampling model)\n",worst);
    printf("MEASURE emergency status: worst=%ums over 120 phases and two AC32 delivery paths\n",worst_emergency);
    puts("PASS E9/profile3: old E2/E5 rejection, emergency stop, atomic status, same-heartbeat restrictions, frozen/reversed replay, modulo-64/clock wrap and unbound");
}
