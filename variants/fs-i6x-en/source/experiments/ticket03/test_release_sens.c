#include "exp3_board.h"
#include "sens_ibus_responder.h"
#include "battery_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reply[SENS_IBUS_MAX_RESPONSE_SIZE], length;
static uint16_t bag_word, bag_full_flags, height_word, fast_word;
uint32_t TelemetrySnapshot_ReadFastAuthorization(void) { return ((uint32_t)0x9001U<<16)|fast_word; }
static bool snapshot_available = true;
static uint32_t sample_at;
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
    s->values[TELEMETRY_VALUE_BAG_FULL] = bag_full_flags;
    s->values[TELEMETRY_VALUE_HEIGHT_FEEDBACK] = height_word;
    s->values[TELEMETRY_VALUE_BATTERY_MIN_CELL] = 2930U;
    s->values[TELEMETRY_VALUE_BATTERY_REASON] = 0x24U;
    s->generated_at_ms = sample_at;
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
    if (command==0xa9U) exp3_board_poll(124U); /* Comm drains the bounded ISR trace. */
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
    /* Diagnostics occupy only idle E1, never E6/E7 or an active result word. */
    g_exp3_downlink_word=0U; sample_at=0U;
    request(&r,0xa9);
    assert(length==6U && (uint16_t)(reply[2]|(reply[3]<<8))==(0x4000U|2930U));
    sample_at=0x300U;
    request(&r,0xa9);
    assert(length==6U && (uint16_t)(reply[2]|(reply[3]<<8))==0x6124U);
    g_exp3_downlink_word=0xe123U;
    request(&r,0xa9);
    assert((uint16_t)(reply[2]|(reply[3]<<8))==0xe123U);
    g_exp3_downlink_word=0xc801U;
    request(&r,0xa9);
    assert((uint16_t)(reply[2]|(reply[3]<<8))==0xc801U);
    snapshot_available=false; g_exp3_downlink_word=0U;
    request(&r,0xa9); assert(!reply[2] && !reply[3]);
    snapshot_available=true;
    exp3_board_poll(124);
    assert(g_exp3_hil.log_write == 0U && g_exp3_capture_overflow == 0U);
    request(&r, 0x8a);
    assert(length==4U && reply[1]==0x8a);
    request(&r, 0x9a);
    assert(length==6U && reply[2]==MOWER_FAST_STATUS_TYPE && reply[3]==4U);
    fast_word=0x0118U;
    request(&r, 0xaa);
    assert(length==8U && reply[2]==0x18 && reply[3]==1U && reply[4]==1U && reply[5]==0x90U);
    fast_word=0x012fU;
    request(&r, 0xaa);
    assert(length==8U && reply[2]==0x2f && reply[3]==1U);
    request(&r, 0x7a);
    assert(length==0U);
    request(&r, 0x8b);
#if MOWER_GRASS_BAG_CH6_ENABLED
    assert(length==4U && reply[1]==0x8b);
    request(&r, 0x9b);
    assert(length==6U && reply[2]==0xe3 && reply[3]==4U);
    bag_word=0x1236U;
    request(&r, 0xab);
    assert(length==8U && reply[2]==0x36 && reply[3]==0x12 && reply[4]==0U && reply[5]==0U);
    bag_word=0x123bU;
    bag_full_flags=3U;
    request(&r, 0xab);
    assert(length==8U && reply[2]==0x3b && reply[3]==0x12 && reply[4]==3U && reply[5]==0U);
    bag_full_flags=2U;
    request(&r, 0xab);
    assert(length==8U && reply[4]==2U && reply[5]==0U);
    snapshot_available=false;
    request(&r, 0xab);
    assert(length==8U && reply[2]==0 && reply[3]==0 && reply[4]==0 && reply[5]==0);
    request(&r, 0x7b);
    assert(length==0U);
#else
    assert(length==0U);
#endif
    snapshot_available=true;
    request(&r, 0x8c); assert(length==4U && reply[1]==0x8c);
    request(&r, 0x9c); assert(length==6U && reply[2]==0xe4 && reply[3]==2U);
    height_word=0x0ac6U;
    request(&r, 0xac); assert(length==6U && reply[2]==0xc6 && reply[3]==0x0a);
    height_word=0x0beeU;
    request(&r, 0xac); assert(length==6U && reply[2]==0xee && reply[3]==0x0b);
    snapshot_available=false;
    request(&r, 0xac); assert(length==6U && !reply[2] && !reply[3]);
    request(&r, 0x7c); assert(length==0U);
    for (unsigned address=13U;address<=15U;++address) {
        request(&r,(uint8_t)(0x80U+address)); assert(length==4U);
        request(&r,(uint8_t)(0x90U+address));
        assert(length==6U && reply[2]==EXP3_COMPLETION_TYPE+address-13U && reply[3]==4U);
        request(&r,(uint8_t)(0xa0U+address));
        assert(length==8U && !reply[2] && !reply[3] && !reply[4] && !reply[5]);
        request(&r,(uint8_t)(0x70U+address)); assert(length==0U);
    }
    puts("PASS release SENS addr13-15 four-byte completion, addr12 height, addr11 connection, addr10 output and unchanged addr1-8");
}
