#include "exp3_board.h"
#include "sens_ibus_responder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reply[6], length;
static uint16_t bag_word;
static bool snapshot_available = true;
#include "main.h"
uint32_t HAL_GetTick(void) { return 123; }
uint32_t HAL_GetUIDw0(void) { return 1U; }
uint32_t HAL_GetUIDw1(void) { return 2U; }
struct FakeSysTick fake_systick = {17U};
static bool snapshot(void *ctx, TelemetrySnapshot_t *s) {
    unsigned i;
    (void)ctx;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < 8; i++)
        s->values[i] = (uint16_t)(100 + i);
    s->values[TELEMETRY_VALUE_BAG_CONNECTION] = bag_word;
    return snapshot_available;
}
static bool send(void *ctx, const uint8_t *p, uint8_t n) {
    (void)ctx;
    memcpy(reply, p, n);
    length = n;
    return true;
}
static void request(SensIbusResponder_t *r, uint8_t command) {
    uint8_t p[4] = {4, command, (uint8_t)(0xfb - command), 0xff};
    length = 0;
    SensIbusResponder_Feed(r, p, 4);
    if (length) {
        uint16_t sum=0xFFFFU;
        unsigned i;
        for (i=0; i<length-2U; ++i) sum=(uint16_t)(sum-reply[i]);
        assert(reply[length-2U]==(uint8_t)sum && reply[length-1U]==(uint8_t)(sum>>8));
    }
}
int main(void) {
    SensIbusResponder_t r;
    unsigned i;
    SensIbusResponder_Init(&r, snapshot, 0, send, 0);
    g_exp3_mailbox.message.controller = 1;
    g_exp3_mailbox.message.hmi = 2;
    g_exp3_mailbox.command = EXP3_ARM;
    exp3_board_poll(0);
    for (i = 1; i <= 8; i++) {
        request(&r, (uint8_t)(0x80 + i));
        assert(length == 4 && reply[1] == 0x80 + i);
        request(&r, (uint8_t)(0xa0 + i));
        assert(length == 6 && reply[2] == 99 + i && reply[3] == 0);
    }
    request(&r, 0x89);
    assert(length == 4 && reply[2] == 0x72 && reply[3] == 0xff);
    request(&r, 0x99);
    assert(length == 6 && reply[2] == 0xe1 && reply[3] == 2);
    g_exp3_downlink_word = 0xabcd;
    request(&r, 0xa9);
    assert(length == 6 && reply[2] == 0xcd && reply[3] == 0xab);
    request(&r, 0x79);
    assert(length == 0);
    exp3_board_poll(124);
    assert(g_exp3_hil.log_write == 0U && g_exp3_capture_overflow == 0U);
    request(&r, 0x8a);
    assert(length==4U && reply[1]==0x8a);
    request(&r, 0x9a);
    assert(length==6U && reply[2]==0xe2 && reply[3]==2U);
    g_blade_ok_runtime_word=0x0118U;
    request(&r, 0xaa);
    assert(length==6U && reply[2]==0x18 && reply[3]==1U);
    g_blade_ok_runtime_word=0x012fU;
    request(&r, 0xaa);
    assert(length==6U && reply[2]==0x2f && reply[3]==1U);
    request(&r, 0x7a);
    assert(length==0U);
    request(&r, 0x8b);
#if MOWER_GRASS_BAG_CH6_ENABLED
    assert(length==4U && reply[1]==0x8b);
    request(&r, 0x9b);
    assert(length==6U && reply[2]==0xe3 && reply[3]==2U);
    bag_word=0x1236U;
    request(&r, 0xab);
    assert(length==6U && reply[2]==0x36 && reply[3]==0x12);
    bag_word=0x123bU;
    request(&r, 0xab);
    assert(length==6U && reply[2]==0x3b && reply[3]==0x12);
    snapshot_available=false;
    request(&r, 0xab);
    assert(length==6U && reply[2]==0 && reply[3]==0);
    request(&r, 0x7b);
    assert(length==0U);
    request(&r,0x8c); assert(length==4U);
    request(&r,0x9c); assert(length==6U && reply[2]==0xe4U && reply[3]==2U);
    g_mower_unlock_generation=0x1234U;
    request(&r,0xac); assert(length==6U && reply[2]==0x34U && reply[3]==0x12U);
    request(&r,0x8d); assert(length==4U);
    request(&r,0x9d); assert(length==6U && reply[2]==0xe5U && reply[3]==2U);
    g_mower_control_word=0xabcdU;
    request(&r,0xad); assert(length==6U && reply[2]==0xcdU && reply[3]==0xabU);
    request(&r,0x8e); assert(length==0U);
#else
    assert(length==0U);
#endif
    puts("PASS release SENS addr11 connection, addr10 output/gear and unchanged addr1-8");
}
