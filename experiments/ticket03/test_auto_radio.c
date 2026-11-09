#include "exp3_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void run_pair(Exp3AutoSession *controller, uint32_t start, uint32_t end)
{
    uint32_t now;
    uint8_t packet[37];
    for (now = start; now < end; now += 10U) {
        uint32_t channels;
        uint16_t uplink, downlink;
        exp3_board_poll(now);
        channels = g_exp3_uplink_channels;
        if (channels && exp3_channel_raw_word((uint16_t)channels,
                                              (uint16_t)(channels >> 16), &uplink))
            (void)exp3_auto_receive(controller, uplink, now, 5000U);
        downlink = exp3_auto_word(controller, now, 250U, 2000U);
        if (downlink && now % 120U == 0U) {
            memset(packet, 0xff, sizeof(packet));
            packet[0] = 0xaa;
            packet[9] = 0xe1;
            packet[10] = 9U;
            packet[11] = (uint8_t)downlink;
            packet[12] = (uint8_t)(downlink >> 8);
            exp3_board_rf(packet, sizeof(packet), now);
        }
    }
}

int main(void)
{
    Exp3AutoSession controller;
    Exp3BoardStatus status;
    uint32_t first_hmi;
    exp3_board_auto_start(0x12345678U, 0U);
    exp3_auto_init(&controller, EXP3_AUTO_CONTROLLER, 0x87654321U, 0U);
    run_pair(&controller, 0U, 10000U);
    exp3_board_status(&status);
    assert(status.bound && status.controller == controller.self);
    assert(controller.peer == status.hmi);
    first_hmi = status.hmi;
    {
        Exp3Message confirm = {0};
        confirm.kind = EXP3_CONFIRM;
        confirm.sender = 1U;
        confirm.controller = status.controller;
        confirm.hmi = status.hmi;
        confirm.request = confirm.action = confirm.generation = 1U;
        confirm.context = EXP3_PWM_CONTEXT;
        confirm.target = EXP3_PWM_TARGET;
        assert(exp3_board_submit(&confirm));
        exp3_board_status(&status);
        assert(status.waiting); /* The mailbox is busy before the RF task polls. */
        assert(!exp3_board_submit(&confirm));
    }
    exp3_auto_init(&controller, EXP3_AUTO_CONTROLLER, 0x87654322U, 10000U);
    run_pair(&controller, 10000U, 20000U);
    exp3_board_status(&status);
    assert(status.bound && status.controller == controller.self && status.hmi == first_hmi);
    puts("PASS OpenI6X RF adapter auto binds and replaces rebooted controller session");
    return 0;
}
