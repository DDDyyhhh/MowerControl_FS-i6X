#include "exp3_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bag_connection.h"

#if MOWER_BLADE_OK_ENABLED
static void control_packet(uint8_t address, uint8_t type, uint16_t word, uint32_t now, bool extended, unsigned length)
{
    uint8_t p[37]; memset(p,0xff,sizeof(p)); p[0]=extended ? 0xac : 0xaa;
    p[9]=type; p[10]=address;
    if (extended) { p[11]=2; p[12]=(uint8_t)word; p[13]=(uint8_t)(word>>8); }
    else { p[11]=(uint8_t)word; p[12]=(uint8_t)(word>>8); }
    exp3_board_rf(p,length,now);
}
static void test_control_feedback(void)
{
    uint16_t generation, status;
    assert(!exp3_board_control(600000U,&generation,&status));
    control_packet(12,0xe4,123U,600000U,false,37U);
    control_packet(13,0xe5,(10U<<6)|34U,600000U,false,37U);
    assert(!exp3_board_control(600000U,&generation,&status));
    control_packet(13,0xe5,(11U<<6)|34U,600100U,true,13U);
    assert(!exp3_board_control(600100U,&generation,&status));
    control_packet(13,0xe5,(11U<<6)|34U,600100U,true,37U);
    assert(exp3_board_control(600100U,&generation,&status) && generation==123U && (status&2U));
    control_packet(13,0xe5,(11U<<6)|34U,600900U,false,37U);
    control_packet(12,0xe4,123U,600900U,false,37U);
    assert(!exp3_board_control(601100U,&generation,&status)); /* Replay cannot renew status. */
    control_packet(13,0xe5,(12U<<6)|34U,601110U,false,37U);
    assert(!exp3_board_control(601110U,&generation,&status));
    control_packet(12,0xe4,124U,601200U,true,37U);
    control_packet(13,0xe5,(13U<<6)|33U,601210U,false,37U);
    assert(exp3_board_control(601210U,&generation,&status) && generation==124U && (status&1U));
    control_packet(13,0xe5,(13U<<6)|32U,601220U,false,37U);
    assert(exp3_board_control(601220U,&generation,&status) && !(status&1U));
    control_packet(13,0xe5,(13U<<6)|33U,601230U,false,37U);
    assert(exp3_board_control(601230U,&generation,&status) && !(status&1U));
    control_packet(13,0xe5,0U,601240U,false,37U);
    assert(!exp3_board_control(601240U,&generation,&status));
    control_packet(12,0xe4,125U,602000U,false,37U);
    control_packet(13,0xe5,(1023U<<6)|34U,602000U,false,37U);
    control_packet(13,0xe5,34U,602100U,false,37U);
    assert(exp3_board_control(602100U,&generation,&status));
    control_packet(12,0xe4,125U,0xfffffff0U,false,37U);
    control_packet(13,0xe5,(100U<<6)|34U,0xfffffff0U,false,37U);
    assert(!exp3_board_control(0xfffffff0U,&generation,&status));
    control_packet(13,0xe5,(101U<<6)|34U,0x54U,false,37U);
    assert(exp3_board_control(0x54U,&generation,&status));
    puts("PASS chassis SENS AA/AC token/status, truncation, frozen replay, lock priority, freshness and wraps");
}
static void bag_packet(uint16_t word, uint32_t now, bool extended, unsigned length)
{
    uint8_t packet[37];
    memset(packet, 0xff, sizeof(packet));
    packet[0]=extended ? 0xac : 0xaa;
    packet[9]=0xe3; packet[10]=11U;
    if (extended) {
        packet[11]=2U; packet[12]=(uint8_t)word; packet[13]=(uint8_t)(word>>8);
    } else {
        packet[11]=(uint8_t)word; packet[12]=(uint8_t)(word>>8);
    }
    exp3_board_rf(packet, length, now);
}

static void test_bag_connection(void)
{
    bool connected=true;
    assert(!exp3_board_bag_connection(400000U, &connected)); /* old firmware */
    assert(!exp3_board_bag_connection(400000U, NULL));
    bag_packet(0x002bU,400000U,false,37U); /* hb10, valid, connected */
    assert(!exp3_board_bag_connection(400000U, &connected));
    bag_packet(0x002bU,400100U,false,37U); /* repeat isn't progress */
    assert(!exp3_board_bag_connection(400100U, &connected));
    bag_packet(0x002fU,400200U,false,37U); /* hb11 */
    assert(exp3_board_bag_connection(400200U, &connected) && connected);
    bag_packet(0x002eU,400210U,false,37U); /* same hb: unplug */
    assert(exp3_board_bag_connection(400210U, &connected) && !connected);
    bag_packet(0x002fU,400220U,false,37U); /* delayed connected */
    assert(exp3_board_bag_connection(400220U, &connected) && !connected);
    bag_packet(0x0033U,400300U,true,13U); /* truncated AC */
    assert(exp3_board_bag_connection(400300U, &connected) && !connected);
    bag_packet(0x0033U,400310U,false,12U); /* truncated AA */
    assert(exp3_board_bag_connection(400310U, &connected) && !connected);
    bag_packet(0x0033U,400320U,true,37U); /* hb12 reconnect */
    assert(exp3_board_bag_connection(400320U, &connected) && connected);
    {
        uint8_t packet[37];
        memset(packet,0xff,sizeof(packet));
        packet[0]=0xaa; packet[9]=0xe3; packet[10]=12U;
        packet[11]=0x36U; packet[12]=0U;
        exp3_board_rf(packet,sizeof(packet),400321U); /* wrong address */
        packet[9]=0xe4; packet[10]=11U;
        exp3_board_rf(packet,sizeof(packet),400322U); /* wrong type */
        packet[0]=0xac; packet[9]=0xe3; packet[11]=1U; packet[12]=0x36U;
        exp3_board_rf(packet,sizeof(packet),400323U); /* wrong payload width */
        assert(exp3_board_bag_connection(400323U,&connected) && connected);
    }
    bag_packet(0x002eU,400330U,false,37U); /* old heartbeat ignored */
    assert(exp3_board_bag_connection(400330U, &connected) && connected);
    bag_packet(0x0033U,401319U,false,37U);
    assert(exp3_board_bag_connection(401319U, &connected) && connected);
    assert(!exp3_board_bag_connection(401320U, &connected)); /* exact 1s */
    bag_packet(0x0033U,401320U,false,37U);
    assert(!exp3_board_bag_connection(401320U, &connected));
    bag_packet(0x0036U,401420U,true,37U); /* recovery needs advance */
    assert(exp3_board_bag_connection(401420U, &connected) && !connected);
    bag_packet(0x0038U,401430U,false,37U); /* invalid sample */
    assert(!exp3_board_bag_connection(401430U, &connected));
    bag_packet(0xffffU,401440U,false,37U); /* baseline at hb16383 */
    assert(!exp3_board_bag_connection(401440U, &connected));
    bag_packet(0x0003U,401540U,true,37U); /* 14-bit wrap */
    assert(exp3_board_bag_connection(401540U, &connected) && connected);
    bag_packet(0x0042U,0xfffffff0U,false,37U); /* time wrap baseline */
    assert(!exp3_board_bag_connection(0xfffffff0U, &connected));
    bag_packet(0x0047U,0x54U,false,37U);
    assert(exp3_board_bag_connection(0x54U, &connected) && connected);
    g_exp3_hil.bound=0U;
    assert(!exp3_board_bag_connection(0x54U, &connected));
    g_exp3_hil.bound=1U;
    puts("PASS grass bag AA/AC feedback, unplug/reconnect, frozen replay, timeout, heartbeat/time wrap");
}
#endif

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

#if MOWER_BLADE_OK_ENABLED
static void test_bag_rebind(Exp3AutoSession *controller)
{
    uint32_t old_controller=g_exp3_hil.controller, now;
    bool connected=false;
    /* Keep the old session's cached PD10 fresh while a new peer handshakes. */
    bag_packet(0x1003U,500000U,false,37U);
    bag_packet(0x1007U,500100U,false,37U);
    assert(exp3_board_bag_connection(500100U,&connected) && connected);
    exp3_auto_init(controller,EXP3_AUTO_CONTROLLER,0x87654323U,500100U);
    for (now=500100U; now<510100U; now+=10U) {
        uint16_t heartbeat=(uint16_t)(1025U+(now-500100U)/100U);
        bag_packet((uint16_t)((heartbeat<<2)|3U),now,false,37U);
        run_pair(controller,now,now+10U);
        if (g_exp3_hil.controller != old_controller) break;
    }
    assert(now<510100U && g_exp3_hil.bound);
    assert(!exp3_board_bag_connection(now,&connected)); /* actual rebind cleared cache */
    bag_packet(0x2003U,now+10U,false,37U);
    assert(!exp3_board_bag_connection(now+10U,&connected));
    bag_packet(0x2006U,now+110U,true,37U);
    assert(exp3_board_bag_connection(now+110U,&connected) && !connected);
    puts("PASS new binding clears fresh grass bag cache and waits for new heartbeat");
}
#endif

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
#if MOWER_BLADE_OK_ENABLED
    {
        uint8_t packet[37];
        uint16_t runtime;
        memset(packet, 0xff, sizeof(packet));
        packet[0]=0xaa; packet[9]=0xe2; packet[10]=10U;
        packet[11]=0x0f; packet[12]=0x01; /* heartbeat16, enabled, high */
        exp3_board_rf(packet, sizeof(packet), 20000U);
        assert(!exp3_board_runtime(20000U, &runtime));
        packet[11]=0x1f;
        exp3_board_rf(packet, sizeof(packet), 20120U);
        assert(exp3_board_runtime(20120U, &runtime) && (runtime&7U)==7U);
        packet[11]=0x18; /* same heartbeat, immediate controller stop */
        exp3_board_rf(packet, sizeof(packet), 20240U);
        assert(exp3_board_runtime(20240U, &runtime) && (runtime&7U)==0U);
        packet[11]=0x1f; /* delayed enabled state from before stop, same heartbeat */
        exp3_board_rf(packet, sizeof(packet), 20300U);
        assert(exp3_board_runtime(20300U, &runtime) && !(runtime&1U));
        packet[11]=0x18;
        exp3_board_rf(packet, sizeof(packet), 21400U);
        assert(!exp3_board_runtime(21400U, &runtime)); /* frozen replay cannot refresh */
        packet[11]=0x0b; packet[12]=0x90; /* long outage: heartbeat2304, low gear */
        exp3_board_rf(packet, sizeof(packet), 300000U);
        assert(!exp3_board_runtime(300000U, &runtime));
        packet[11]=0x1b;
        exp3_board_rf(packet, sizeof(packet), 300120U);
        assert(exp3_board_runtime(300120U, &runtime) && (runtime&7U)==3U);
        /* Extended RF packet, medium gear; truncated payload cannot update. */
        packet[0]=0xac; packet[11]=2U; packet[12]=0x2d; packet[13]=0x90;
        exp3_board_rf(packet, 13U, 300200U);
        assert(exp3_board_runtime(300200U, &runtime) && (runtime&7U)==3U);
        exp3_board_rf(packet, sizeof(packet), 300240U);
        assert(exp3_board_runtime(300240U, &runtime) && (runtime&7U)==5U);
    }
#endif
#if MOWER_BLADE_OK_ENABLED
    test_bag_connection();
#endif
    {
        Exp3Message cancel = {0};
        g_exp3_mailbox.command=0U;
        g_exp3_hil.waiting=1U; g_exp3_hil.link.tx_active=0U;
        g_exp3_hil.pending=(Exp3Message){.kind=EXP3_CONFIRM, .sender=1U,
            .controller=g_exp3_hil.controller, .hmi=g_exp3_hil.hmi,
            .request=9U, .action=9U, .generation=9U,
            .context=EXP3_PWM_CONTEXT, .target=EXP3_PWM_TARGET};
        cancel=g_exp3_hil.pending; cancel.kind=EXP3_CANCEL;
        cancel.action=10U;
        assert(!exp3_board_submit(&cancel) && g_exp3_hil.waiting);
        cancel.action=9U; cancel.sender=2U;
        assert(!exp3_board_submit(&cancel) && g_exp3_hil.waiting);
        cancel.sender=1U; g_exp3_hil.link.tx_active=1U;
        assert(!exp3_board_submit(&cancel) && g_exp3_hil.waiting);
        g_exp3_hil.link.tx_active=0U;
        assert(exp3_board_submit(&cancel)); // lost Confirm reply must not block stop
        exp3_board_poll(310000U);
        assert(g_exp3_hil.pending.kind==EXP3_CANCEL);
        for (uint32_t t=310020U; t<=312000U; t+=20U) exp3_board_poll(t);
        uint32_t transfer=g_exp3_hil.link.transfer;
        exp3_board_poll(318000U);
        assert(g_exp3_hil.link.transfer==transfer+1U); // lost Cancel result retry
        assert(g_exp3_hil.pending.action==9U && g_exp3_hil.pending.generation==9U);
        for (uint32_t t=318020U; t<=320000U; t+=20U) exp3_board_poll(t);
        exp3_board_poll(326000U);
        assert(g_exp3_hil.link.transfer==transfer+2U);
        for (uint32_t t=326020U; t<=328000U; t+=20U) exp3_board_poll(t);
        exp3_board_poll(334000U);
        assert(g_exp3_hil.link.transfer==transfer+2U); // bounded retries only
        exp3_board_poll(370000U);
        assert(g_exp3_hil.waiting && g_exp3_hil.cancel_failed); // fail closed at absolute deadline
        Exp3Link reply={0}; Exp3Message result=cancel;
        result.kind=EXP3_RESULT; result.sender=2U; result.command=EXP3_CANCEL;
        result.status=EXP3_UNKNOWN;
        assert(exp3_link_send_result(&reply,&result,371000U));
        for (uint32_t t=371000U; reply.tx_active; t+=20U) {
            uint16_t w=exp3_link_word(&reply,t,20U);
            if(w) exp3_hil_receive(&g_exp3_hil,NULL,w,t);
        }
        assert(g_exp3_hil.waiting && g_exp3_hil.cancel_failed);
        result.status=EXP3_CANCELLED;
        assert(exp3_link_send_result(&reply,&result,372000U));
        for (uint32_t t=372000U; reply.tx_active; t+=20U) {
            uint16_t w=exp3_link_word(&reply,t,20U);
            if(w) exp3_hil_receive(&g_exp3_hil,NULL,w,t);
        }
        assert(!g_exp3_hil.waiting && !g_exp3_hil.cancel_failed);

    }
#if MOWER_BLADE_OK_ENABLED
    test_bag_rebind(&controller);
    test_control_feedback();
#endif
    puts("PASS OpenI6X RF adapter auto binds and replaces rebooted controller session");
    return 0;
}
