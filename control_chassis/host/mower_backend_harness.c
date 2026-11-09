#include "remote_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static uint16_t channels[14];
static uint32_t clock_now(void *ctx) { (void)ctx; return now; }
static void frame(void) {
    uint8_t bytes[32]={32,64}; uint16_t sum=0xffffU;
    for(unsigned i=0;i<14;++i) { bytes[2+2*i]=(uint8_t)channels[i]; bytes[3+2*i]=(uint8_t)(channels[i]>>8); }
    for(unsigned i=0;i<30;++i) sum=(uint16_t)(sum-bytes[i]);
    bytes[30]=(uint8_t)sum; bytes[31]=(uint8_t)(sum>>8);
    now+=10U; RemoteBackend_OnUartRx(1U,bytes,32U);
    RemoteBackend_Advance(now,(RemoteSafetyInput_t){false,false,false,false});
}
static void frames(unsigned count) { while(count--) frame(); }
int main(void) {
    RemoteCommand_t command;
    RemoteBackend_Init((RemoteBackendClock_t){clock_now,NULL});
    MowerModeControl_t *mode=RemoteBackend_ModeControl();
    MowerMode_Session(mode,1U,2U);
    for(unsigned i=0;i<14;++i) channels[i]=1500U;
    channels[4]=2000U; channels[5]=1000U; channels[6]=2000U; channels[7]=1000U;
    channels[8]=1700U; channels[9]=1600U;
    frames(50);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.connected && command.mode_valid && command.selected_mode==MOWER_SELECT_RC);
    assert(!command.run_permitted && mode->ready && command.blade_gear==REMOTE_BLADE_GEAR_LOW);
    assert(MowerMode_Confirm(mode,mode->generation)); frames(3);
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    assert(command.travel_speed_mps>1.2f && command.travel_speed_mps<1.3f);
    assert(command.height_level==4U && !command.has_confirm_event);
    channels[5]=2000U; frames(3); RemoteBackend_GetCommand(&command);
    assert(!command.has_confirm_event); /* CH6 cannot serve either OK context. */
    channels[6]=1000U; frames(3); RemoteBackend_GetCommand(&command);
    assert(command.blade_gear==REMOTE_BLADE_GEAR_HIGH);
    channels[7]=2000U; frames(3); RemoteBackend_GetCommand(&command);
    assert(command.selected_mode==MOWER_SELECT_API && mode->armed);
    channels[7]=1500U; frames(4); RemoteBackend_GetCommand(&command);
    assert(command.selected_mode==MOWER_SELECT_RC && command.run_permitted);
    channels[4]=1000U; frames(3); RemoteBackend_GetCommand(&command);
    assert(!command.run_permitted);
    channels[4]=2000U; frames(30); assert(!mode->armed && mode->ready);
    assert(MowerMode_Confirm(mode,mode->generation)); frames(3);
    now+=100U; RemoteBackend_Advance(now,(RemoteSafetyInput_t){false,false,false,false});
    assert(!mode->armed);
    puts("PASS production i-BUS CH8/OK backend, CH7, CH9, CH10, CH6 isolation and timeout");
}
