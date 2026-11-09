#include "mower_mode_control.h"
#include <assert.h>
#include <stdio.h>
static MowerModeControl_t s;
static MowerModeInput_t in;
static uint32_t now;
static void advance(void) { now+=10U; ++in.frame_sequence; MowerMode_Advance(&s,&in,now); }
static void frames(unsigned n) { while(n--) advance(); }
static void reset(void) {
    MowerMode_Init(&s); now=0U;
    in=(MowerModeInput_t){true,true,false,true,true,1000U,0U};
    MowerMode_Session(&s,11U,22U); frames(30U);
    assert(s.selected==MOWER_SELECT_RC && !s.armed && s.ready);
}
static void unlock(void) {
    uint16_t epoch=s.generation;
    assert(MowerMode_Confirm(&s,epoch));
    assert(!MowerMode_Confirm(&s,epoch) && !s.permitted);
    advance(); assert(s.permitted);
}
int main(void) {
    reset();
    in.enabled=false; in.mode_raw=2000U; frames(3);
    assert(s.mode_valid && s.selected==MOWER_SELECT_API && !s.armed && !s.ready);
    in.safety_active=true; in.mode_raw=1000U; frames(3);
    assert(s.mode_valid && s.selected==MOWER_SELECT_RC && !s.armed);
    in.safety_active=false; in.enabled=true; frames(22);
    uint16_t abandoned=s.generation;
    in.blade_centered=false; advance(); in.blade_centered=true; frames(22);
    assert(!MowerMode_Confirm(&s,abandoned));
    in.mode_raw=2000U; frames(3); in.mode_raw=1000U; frames(3); frames(25);
    assert(!s.armed); /* The old CH8 gesture grants nothing. */
    assert(!MowerMode_Host(&s,1,now)); assert(!MowerMode_Host(&s,2,now));
    assert(!MowerMode_Confirm(&s,s.generation-1U));
    unlock();
    assert(!MowerMode_Host(&s,1,now)); assert(s.api_mode==0U);
    in.mode_raw=2000U;
    uint32_t before=s.mode_sequence;
    advance(); MowerMode_Advance(&s,&in,now); MowerMode_Advance(&s,&in,now);
    assert(s.mode_sequence==before); /* Duplicate snapshots are not debounce frames. */
    frames(2); assert(s.selected==MOWER_SELECT_API && s.armed && !s.permitted);
    advance(); assert(!MowerMode_Host(&s,2,now));
    MowerMode_Host(&s,1,now); assert(s.api_mode==1U);
    assert(MowerMode_Host(&s,2,now)); assert(s.api_mode==2U);
    in.mode_raw=1500U; frames(3);
    assert(s.armed && s.selected==MOWER_SELECT_RC && s.api_mode==0U);
    advance();
    in.mode_raw=2000U; frames(4); MowerMode_Host(&s,1,now); MowerMode_Host(&s,2,now);
    in.centered=false; advance(); assert(!s.armed && !s.api_mode);
    assert(!MowerMode_Host(&s,1,now)); assert(!MowerMode_Confirm(&s,s.generation));
    in.centered=true; frames(22); unlock();
    MowerMode_Host(&s,1,now); MowerMode_Host(&s,2,now);
    now+=500U; MowerMode_Advance(&s,&in,now); assert(!s.armed);
    frames(22); unlock();
    uint16_t old=s.generation;
    in.safety_active=true; advance(); assert(!s.armed && !s.ready);
    in.safety_active=false; frames(22); assert(!MowerMode_Confirm(&s,old)); unlock();
    in.enabled=false; advance(); assert(!s.armed);
    in.enabled=true; frames(22); unlock();
    in.online=false; advance(); assert(!s.armed && !s.mode_valid);
    in.online=true; frames(26); unlock();
    in.mode_raw=1750U; advance(); assert(!s.armed && !s.mode_valid);
    in.mode_raw=1000U; frames(26); unlock();
    old=s.generation; assert(MowerMode_Cancel(&s,old));
    assert(!s.armed && !MowerMode_Confirm(&s,old));
    frames(22); unlock(); MowerMode_Session(&s,12,22); assert(!s.armed);
    frames(22); s.generation=UINT16_MAX; MowerMode_Stop(&s);
    assert(s.generation_exhausted && !MowerMode_Confirm(&s,1U));
    MowerMode_Session(&s,13,22); assert(s.generation==1U && !s.generation_exhausted);
    now=UINT32_MAX-100U; s.center_tracking=false; frames(22); assert(s.ready);
    puts("PASS CH8 mode/OK authorization, handoff, API handshake/override/timeout, all locks, epochs and wrap");
}
