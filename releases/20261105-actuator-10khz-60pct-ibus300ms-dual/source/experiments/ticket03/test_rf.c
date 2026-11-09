#include "exp3_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    Exp3Message m = {0};
    Exp3Link tx = {0};
    unsigned i;
    uint8_t p[37];
    uint16_t w;
    m.controller = 10;
    m.hmi = 20;
    m.request = 1;
    m.action = 1;
    m.context = 1;
    m.generation = 1;
    m.target = 1;
    m.kind = EXP3_CONFIRM;
    m.sender = 1;
    g_exp3_mailbox.message = m;
    g_exp3_mailbox.command = EXP3_ARM;
    exp3_board_poll(0);
    g_exp3_mailbox.command = EXP3_SEND;
    exp3_board_poll(1);
    m.kind = EXP3_RESULT;
    m.sender = 2;
    m.command = EXP3_CONFIRM;
    m.status = EXP3_ACCEPTED;
    assert(exp3_link_send(&tx, &m, 10));
    for (i = 0; i < 90; i++) {
        w = exp3_link_word(&tx, 10 + i * 250, 250);
        memset(p, 0xff, sizeof p);
        if (i % 2) { /* actual AC format: id, instance, size, payload */
            p[0] = 0xac;
            p[9] = 0xe1;
            p[10] = 9;
            p[11] = 2;
            p[12] = (uint8_t)w;
            p[13] = w >> 8;
        } else {
            p[0] = 0xaa;
            p[9] = 0xe1;
            p[10] = 9;
            p[11] = (uint8_t)w;
            p[12] = w >> 8;
        }
        exp3_board_capture_rf(p, sizeof p, 10 + i * 250);
        exp3_board_poll(11 + i * 250);
    }
    assert(g_exp3_hil.matches == 1 && g_exp3_hil.last_result.action == 1);
    /* Bounds and unrelated slots cannot fabricate another result. */
    for (i = 0; i < 37; i++)
        exp3_board_rf(p, i, 30000);
    memset(p, 0xff, sizeof p);
    p[0] = 0xac;
    p[9] = 0xe1;
    p[10] = 9;
    p[11] = 255;
    exp3_board_rf(p, sizeof p, 30001);
    assert(g_exp3_hil.matches == 1);
    exp3_board_capture_rf(p, sizeof p, 31000);
    exp3_board_capture_rf(p, sizeof p, 31001);
    exp3_board_capture_rf(p, sizeof p, 31002);
    assert(g_exp3_capture_overflow == 1);
    exp3_board_poll(31003);
    /* The same real AA/AC RF boundary decodes the shorter bench result. */
    m.kind = EXP3_CONFIRM;
    m.sender = 1U;
    m.status = m.command = 0U;
    m.request = m.action = m.generation = 2U;
    m.context = EXP3_PWM_CONTEXT;
    m.target = EXP3_PWM_TARGET;
    assert(exp3_board_submit(&m));
    exp3_board_poll(40000U);
    m.kind = EXP3_RESULT;
    m.sender = 2U;
    m.command = EXP3_CONFIRM;
    m.status = EXP3_ACCEPTED;
    exp3_link_init(&tx);
    assert(exp3_link_send_result(&tx, &m, 40010U));
    for (i = 0U; i < 17U; ++i) {
        w = exp3_link_word(&tx, 40010U + i * 250U, 250U);
        memset(p, 0xff, sizeof p);
        p[0] = i % 2U ? 0xac : 0xaa;
        p[9] = 0xe1;
        p[10] = 9U;
        if (i % 2U) {
            p[11] = 2U;
            p[12] = (uint8_t)w;
            p[13] = w >> 8;
        } else {
            p[11] = (uint8_t)w;
            p[12] = w >> 8;
        }
        exp3_board_capture_rf(p, sizeof p, 40010U + i * 250U);
        exp3_board_poll(40011U + i * 250U);
    }
    assert(g_exp3_hil.matches == 2U && !g_exp3_hil.waiting);
    assert(g_exp3_hil.last_result.action == 2U);
    puts("PASS actual AA/AC adapter, foreground queue/overflow, truncated RF bounds");
}
