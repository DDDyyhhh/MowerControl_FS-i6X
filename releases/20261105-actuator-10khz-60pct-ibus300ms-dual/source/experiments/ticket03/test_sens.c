#include "exp3_board.h"
#include "sens_ibus_responder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reply[6], length;
uint32_t HAL_GetTick(void) { return 123; }
static bool snapshot(void *ctx, TelemetrySnapshot_t *s) {
    unsigned i;
    (void)ctx;
    for (i = 0; i < 8; i++)
        s->values[i] = (uint16_t)(100 + i);
    return true;
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
    assert(g_exp3_hil.log_write > 1 && g_exp3_capture_overflow == 0);
    puts("PASS actual SENS adapter addr9 and unchanged addr1-8");
}
