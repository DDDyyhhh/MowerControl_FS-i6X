#include "control_source.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ControlSource_t s;
static ControlSourceInput_t i;
static HostControlSnapshot_t h;
static void step(unsigned dt) {
    i.now_ms += dt; i.frame_at_ms = i.now_ms; ++i.frame_sequence;
    ControlSource_Advance(&s, &i, &h);
}
static void host(unsigned mode) {
    uint8_t p[8] = {(uint8_t)mode, 5, 0, 2, 3, 4, 1, 0xAA};
    assert(HostControl_Publish(&h,p,8,i.now_ms));
}
static void select(unsigned raw) { i.ch8=raw; for(unsigned n=0;n<3;++n)step(10); }
static void center(void) { i.steering=i.throttle=1500; for(unsigned n=0;n<24;++n)step(10); }
static void reset(unsigned tick) {
    ControlSource_Reset(&s); memset(&h,0,sizeof(h));
    i=(ControlSourceInput_t){.now_ms=tick,.steering=1500,.throttle=1500,.ch8=1000,
        .link_ok=true,.authorized=true};
    select(1000); center(); assert(s.remote_ready && ControlSource_CanUnlock(&s));
}
static void run(void) {
    select(2000); assert(s.mode==1 && s.stop && !s.remote_ready && !s.api_ready);
    step(10); host(2); step(10); assert(s.mode==1);
    host(1); step(10); host(2); step(10);
    assert(s.mode==2 && s.stop && !s.api_ready);
    host(2); step(10); assert(s.api_ready && s.command.blade==3 && s.command.height==4);
}
int main(void) {
    reset(0);
    /* CAN cannot take ownership or unlock via magic while CH8 selects RC. */
    host(1);step(10);host(2);step(10);assert(s.mode==0 && s.remote_ready);
    select(1500);assert(s.remote_ready && !s.stop); /* Both positions are RC. */
    run();
    i.throttle=1551;step(1);assert(s.takeover && s.mode==0 && s.stop && !s.remote_ready);
    host(1);step(10);host(2);step(10);assert(s.takeover && s.mode==0);
    center();assert(s.remote_ready && s.display==CONTROL_DISPLAY_TAKEOVER);
    select(1000);assert(!s.takeover);run();
    select(1000);assert(s.stop && !s.remote_ready);center();assert(s.remote_ready);
    run();host(0);host(2);step(10);assert(s.mode==1 && s.stop && !s.api_ready);
    host(2);step(10);assert(s.mode==1); /* Exit event cannot be overwritten away. */
    host(1);step(10);host(2);step(10);host(2);step(10);assert(s.api_ready);
    i.now_ms=h.received_at_ms+CONTROL_API_TIMEOUT_MS;step(0);
    assert(s.revoke && s.stop && s.mode==0 && s.takeover);
    i.authorized=false;center();assert(!s.remote_ready && !ControlSource_CanUnlock(&s));
    select(1000);assert(ControlSource_CanUnlock(&s));
    i.authorized=true;center();assert(s.remote_ready);
    run(); i.ch8=1750;step(1);assert(!s.valid && s.stop && !s.api_ready);
    select(2000);assert(s.mode==1 && !s.api_ready);
    host(2);step(10);assert(s.mode==1);
    /* Delayed CAN delivery stamped before the selection is not a handshake. */
    h=(HostControlSnapshot_t){0};
    uint8_t p[8]={1,0,0,0,0,1,0,0};
    HostControl_Publish(&h,p,8,s.barrier_ms-1);p[0]=2;
    HostControl_Publish(&h,p,8,s.barrier_ms-1);step(10);assert(s.mode==1);
    reset(0);i.ch8=2000;step(10);assert(s.mode==0);
    for(unsigned n=0;n<20;++n)ControlSource_Advance(&s,&i,&h);
    assert(s.mode==0);step(10);assert(s.mode==0);step(10);assert(s.mode==1);
    /* Every decode boundary, including gaps, fails closed or selects correctly. */
    const unsigned values[]={899,900,1120,1121,1379,1380,1620,1621,1879,1880,2100,2101};
    const unsigned expected[]={0,1,1,0,0,1,1,0,0,2,2,0};
    for(unsigned n=0;n<sizeof(values)/sizeof(values[0]);++n){
        reset(0);select(values[n]);assert(s.valid==(expected[n]!=0));
        if(s.valid)assert(s.requested==expected[n]);
    }
    reset(UINT32_MAX-330);run(); /* Handshake crosses the clock wrap. */
    assert(i.now_ms<1000U);
    i.frame_sequence=UINT32_MAX;step(10);step(10);assert(s.mode==2);
    i.link_ok=false;step(1);assert(s.stop && !s.api_ready && !s.valid);
    i.link_ok=true;i.authorized=false;select(2000);host(1);step(10);host(2);step(10);
    assert(s.mode==1 && !s.api_ready && !ControlSource_CanUnlock(&s));
    /* Invalid CAN frames neither refresh the heartbeat nor change events. */
    HostControlSnapshot_t old=h;p[0]=3;assert(!HostControl_Publish(&h,p,8,0));
    p[0]=1;assert(!HostControl_Publish(&h,p,7,0));assert(!HostControl_Publish(&h,p,9,0));
    assert(memcmp(&h,&old,sizeof(h))==0);
    /* Ordered 1/2 arriving in one millisecond are distinguished by sequence. */
    reset(0);select(2000);step(10);host(1);host(2);step(0);
    assert(s.mode==2 && !s.api_ready);host(2);step(0);assert(s.api_ready);
    /* CAN publisher skips sequence zero; a missing exit is not an old event. */
    reset(0); h.sequence=UINT32_MAX-2U; select(2000); step(10);
    host(1);step(10);host(2);step(10);assert(s.mode==2 && !s.api_ready);
    host(2);step(10);assert(h.sequence==1 && s.api_ready);
    /* No standby must remain no standby when both clock and sequence wrap. */
    reset(UINT32_MAX-300); h.sequence=UINT32_MAX;select(2000);step(10);
    host(2);step(10);assert(s.mode==1 && !s.api_ready && h.standby_sequence==0);
    puts("PASS CH8 boundaries/debounce, RC lockout, ordered CAN events, takeover, freshness, timeout and wrap");
    return 0;
}
