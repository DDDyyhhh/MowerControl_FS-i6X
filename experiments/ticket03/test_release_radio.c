#include "exp3_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bag_connection.h"
#include "height_feedback.h"
#include "battery_status.h"

#if MOWER_BLADE_OK_ENABLED
static void battery_packet(uint16_t word, uint32_t now, bool extended, unsigned length) {
    uint8_t packet[37]; memset(packet,0xff,sizeof(packet));
    packet[0]=extended?0xacU:0xaaU; packet[9]=0xe1U; packet[10]=9U;
    unsigned offset=11U;
    if(extended) packet[offset++]=2U;
    packet[offset++]=(uint8_t)word; packet[offset]=(uint8_t)(word>>8U);
    exp3_board_rf(packet,length,now);
}
static void test_battery_diagnostics(void) {
    uint16_t minimum=0U,reasons=0U;
    Exp3Link link=g_exp3_hil.link;
    uint32_t matches=g_exp3_hil.matches;
    battery_packet(0x6124U,100U,false,37U); /* no preceding minimum */
    assert(!exp3_board_battery(100U,&minimum,&reasons));
    battery_packet(0x4000U|2930U,110U,false,37U);
    battery_packet(0x6124U,120U,true,37U); /* first heartbeat only */
    assert(!exp3_board_battery(120U,&minimum,&reasons));
    battery_packet(0x4000U|2920U,610U,true,37U);
    battery_packet(0x6224U,620U,false,37U);
    assert(exp3_board_battery(620U,&minimum,&reasons) && minimum==2920U && reasons==0x24U);
    battery_packet(0x4000U|3000U,700U,false,37U);
    assert(exp3_board_battery(700U,&minimum,&reasons) && minimum==2920U); /* half pair hidden */
    battery_packet(0x6320U,710U,true,13U); /* truncated AC */
    battery_packet(0x6124U,720U,false,37U); /* backwards heartbeat */
    battery_packet(0x6224U,750U,false,37U); /* frozen heartbeat */
    assert(exp3_board_battery(750U,&minimum,&reasons) && minimum==2920U);
    battery_packet(0x6320U,1710U,false,37U); /* pair exceeded 1000ms */
    assert(exp3_board_battery(3619U,&minimum,&reasons));
    assert(!exp3_board_battery(3620U,&minimum,&reasons));
    battery_packet(0x4000U|3000U,3700U,false,37U);
    battery_packet(0x6224U,3710U,false,37U);
    assert(!exp3_board_battery(3710U,&minimum,&reasons)); /* replay is a baseline, not valid */
    battery_packet(0x4000U|5001U,3800U,false,37U); /* invalid minimum */
    battery_packet(0x6320U,3810U,false,37U);
    assert(!exp3_board_battery(3810U,&minimum,&reasons));
    battery_packet(0x4000U|3000U,3900U,false,37U);
    battery_packet(0x6320U,3910U,true,37U);
    assert(exp3_board_battery(3910U,&minimum,&reasons) && minimum==3000U && reasons==0x20U);
    assert(!exp3_board_battery(3910U,NULL,&reasons));
    assert(!exp3_board_battery(3910U,&minimum,NULL));
    battery_packet(0x4000U|3000U,0xfffffdf0U,false,37U);
    battery_packet(0x7f01U,0xfffffe00U,true,37U); /* time gap sets heartbeat baseline 31 */
    assert(!exp3_board_battery(0xfffffe00U,&minimum,&reasons));
    battery_packet(0x4000U,0x10U,false,37U);
    battery_packet(0x6001U,0x20U,true,37U); /* time + heartbeat wrap */
    assert(exp3_board_battery(0x20U,&minimum,&reasons) && !minimum && reasons==1U);
    assert(!exp3_board_battery(0x20U+3000U,&minimum,&reasons));
    assert(g_exp3_hil.matches==matches && memcmp(&link,&g_exp3_hil.link,sizeof(link))==0);
    puts("PASS idle E1 battery AA/AC diagnostics: coherent pair, forward heartbeat, stale/replay/truncation, time/wrap; no result parser mutation");
}

static void completion_packet(const Exp3Message *message, uint8_t sequence,
                              unsigned part, uint32_t now, unsigned length) {
    uint8_t bytes[EXP3_COMPLETION_BYTES], packet[37];
    assert(exp3_completion_encode(message,bytes));
    uint32_t word=exp3_completion_part(bytes,sequence,part);
    memset(packet,0xff,sizeof(packet));
    packet[0]=0xac; packet[9]=(uint8_t)(EXP3_COMPLETION_TYPE+part);
    packet[10]=(uint8_t)(EXP3_COMPLETION_ADDRESS+part); packet[11]=4U;
    for(unsigned i=0;i<4U;i++) packet[12U+i]=(uint8_t)(word>>(8U*i));
    exp3_board_rf(packet,length,now);
}
static void test_completion_rejection(void) {
    uint32_t now=800000U, matches=g_exp3_hil.matches;
    Exp3Message pending={.kind=EXP3_CONFIRM,.sender=1U,
        .controller=g_exp3_hil.controller,.hmi=g_exp3_hil.hmi,
        .request=100U,.action=100U,.generation=100U,.context=EXP3_PWM_CONTEXT,.target=1U};
    g_exp3_hil.waiting=0U; g_exp3_hil.link.tx_active=0U;
    g_exp3_mailbox.command=0U;
    assert(exp3_board_submit(&pending));
    exp3_board_poll(now);
    Exp3Message result=pending;
    result.kind=EXP3_RESULT; result.sender=2U; result.command=EXP3_CONFIRM; result.status=EXP3_ACCEPTED;
    for(unsigned i=0;i<3U;i++) completion_packet(&result,1U,i,now+1U,37U);
    assert(g_exp3_hil.waiting && g_exp3_hil.matches==matches); // Still transmitting.
    for(unsigned i=1;i<=20U;i++) exp3_board_poll(now+20U*i);
    assert(!g_exp3_hil.link.tx_active && g_exp3_hil.waiting);
    for(unsigned scenario=0;scenario<6U;scenario++) {
        Exp3Message wrong=result;
        if(scenario==0U) wrong.controller++;
        if(scenario==1U) wrong.hmi++;
        if(scenario==2U) wrong.action=wrong.request=99U;
        if(scenario==3U) wrong.generation++;
        if(scenario==4U) wrong.context=EXP3_UNLOCK_CONTEXT;
        if(scenario==5U) wrong.command=EXP3_CANCEL;
        for(unsigned i=0;i<3U;i++) completion_packet(&wrong,(uint8_t)(2U+scenario),i,now+410U+scenario*10U+i,37U);
        assert(g_exp3_hil.waiting && g_exp3_hil.matches==matches);
    }
    completion_packet(&result,8U,0U,now+500U,37U);
    completion_packet(&result,8U,1U,now+501U,15U); // Truncated four-byte value.
    completion_packet(&result,8U,2U,now+502U,37U);
    assert(g_exp3_hil.waiting);
    completion_packet(&result,8U,1U,now+503U,37U);
    assert(!g_exp3_hil.waiting && g_exp3_hil.matches==matches+1U);
    for(unsigned i=0;i<3U;i++) completion_packet(&result,8U,i,now+504U+i,37U);
    assert(g_exp3_hil.matches==matches+1U); // Duplicate is observational only.
    pending.kind=EXP3_CANCEL;
    assert(exp3_board_submit(&pending));
    for(unsigned i=0;i<=20U;i++) exp3_board_poll(now+600U+20U*i);
    result.command=EXP3_CANCEL; result.status=EXP3_UNKNOWN;
    for(unsigned i=0;i<3U;i++) completion_packet(&result,9U,i,now+1010U+i,37U);
    assert(g_exp3_hil.waiting && g_exp3_hil.cancel_failed && g_exp3_hil.matches==matches+1U);
    result.status=EXP3_CANCELLED;
    for(unsigned i=0;i<3U;i++) completion_packet(&result,10U,i,now+1020U+i,37U);
    assert(!g_exp3_hil.waiting && !g_exp3_hil.cancel_failed && g_exp3_hil.matches==matches+2U);
    puts("PASS actual RF completion: busy frame, wrong session/action/generation/context/command, truncated/lost part, duplicate and non-Cancelled rejection");
}

static void height_packet(uint16_t word, uint32_t now, bool extended, unsigned length)
{
    uint8_t packet[37];
    memset(packet, 0xff, sizeof(packet));
    packet[0]=extended ? 0xac : 0xaa;
    packet[9]=MOWER_HEIGHT_SENS_TYPE; packet[10]=MOWER_HEIGHT_SENS_ADDRESS;
    if (extended) {
        packet[11]=2U; packet[12]=(uint8_t)word; packet[13]=(uint8_t)(word>>8);
    } else {
        packet[11]=(uint8_t)word; packet[12]=(uint8_t)(word>>8);
    }
    exp3_board_rf(packet,length,now);
}

static void test_height_feedback(void)
{
    uint16_t mm=0;
    assert(!exp3_board_height(600000U,&mm));
    assert(!exp3_board_height(600000U,NULL));
    height_packet(0x0ac6U,600000U,false,37U);
    assert(!exp3_board_height(600000U,&mm)); /* first packet is only a baseline */
    height_packet(0x0ac6U,600100U,false,37U);
    assert(!exp3_board_height(600100U,&mm));
    height_packet(0x0bc6U,600200U,true,37U);
    assert(exp3_board_height(600200U,&mm) && mm==70U);
    height_packet(0x0beeU,600210U,false,37U); /* same heartbeat cannot move height */
    assert(exp3_board_height(600210U,&mm) && mm==70U);
    height_packet(0x0caaU,600300U,false,37U); /* 4.2cm */
    assert(exp3_board_height(600300U,&mm) && mm==42U);
    height_packet(0x0deeU,600310U,false,12U); /* truncated AA */
    height_packet(0x0deeU,600320U,true,13U); /* truncated AC */
    assert(exp3_board_height(600320U,&mm) && mm==42U);
    {
        uint8_t p[37];
        memset(p,0xff,sizeof(p));
        p[0]=0xaa; p[9]=MOWER_HEIGHT_SENS_TYPE; p[10]=13U;
        p[11]=0xee; p[12]=0x0d;
        exp3_board_rf(p,sizeof(p),600330U);
        p[9]=0xe5; p[10]=MOWER_HEIGHT_SENS_ADDRESS;
        exp3_board_rf(p,sizeof(p),600340U);
        p[0]=0xac; p[9]=MOWER_HEIGHT_SENS_TYPE; p[11]=1U; p[12]=0xee;
        exp3_board_rf(p,sizeof(p),600350U);
        assert(exp3_board_height(600350U,&mm) && mm==42U);
    }
    height_packet(0x0bc6U,600360U,false,37U); /* older heartbeat */
    height_packet(0x0caaU,601299U,false,37U); /* frozen packet cannot renew */
    assert(exp3_board_height(601299U,&mm) && mm==42U);
    assert(!exp3_board_height(601300U,&mm));
    height_packet(0x0caaU,601300U,false,37U);
    assert(!exp3_board_height(601300U,&mm));
    height_packet(0x0deeU,601400U,true,37U);
    assert(exp3_board_height(601400U,&mm) && mm==110U);
    height_packet(0x0e00U,601410U,false,37U); /* invalid producer sample */
    assert(!exp3_board_height(601410U,&mm));
    height_packet(0x0fffU,601420U,false,37U); /* height 127mm is out of model range */
    assert(!exp3_board_height(601420U,&mm));
    height_packet(0xffeeU,601500U,false,37U);
    height_packet(0x0094U,601600U,true,37U); /* heartbeat wrap, 2.0cm */
    assert(exp3_board_height(601600U,&mm) && mm==20U);
    height_packet(0x01c6U,0xfffffff0U,false,37U); /* time wrap baseline */
    assert(!exp3_board_height(0xfffffff0U,&mm));
    height_packet(0x02c6U,0x54U,false,37U);
    assert(exp3_board_height(0x54U,&mm) && mm==70U);
    assert(!exp3_board_height(0x43cU,&mm)); /* exact 1000ms after progress */
    g_exp3_hil.bound=0;
    assert(!exp3_board_height(0x54U,&mm));
    g_exp3_hil.bound=1;
    puts("PASS controller height AA/AC, no replay refresh, invalid range, time/heartbeat wrap");
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

static void bag_full_packet(uint16_t heartbeat, uint16_t flags, uint32_t now,
                            unsigned length, bool connected)
{
    uint8_t packet[37];
    uint16_t connection=(uint16_t)((heartbeat<<2) | MOWER_BAG_SAMPLE_VALID |
        (connected ? MOWER_BAG_CONNECTED : 0U));
    memset(packet, 0xff, sizeof(packet));
    packet[0]=0xac; packet[9]=MOWER_BAG_SENS_TYPE;
    packet[10]=MOWER_BAG_SENS_ADDRESS; packet[11]=4U;
    packet[12]=(uint8_t)connection; packet[13]=(uint8_t)(connection>>8);
    packet[14]=(uint8_t)flags; packet[15]=(uint8_t)(flags>>8);
    exp3_board_rf(packet, length, now);
}

static void test_bag_full(void)
{
    bool full=true, connected=false;
    assert(!exp3_board_bag_full(600000U, &full));
    assert(!exp3_board_bag_full(600000U, NULL));
    bag_packet(43U,600000U,false,37U);
    bag_packet(47U,600100U,true,37U);
    assert(exp3_board_bag_connection(600100U,&connected) && connected);
    assert(!exp3_board_bag_full(600100U,&full)); /* legacy cannot invent empty */
    bag_full_packet(12U,2U,600200U,37U,true);
    assert(!exp3_board_bag_full(600200U,&full)); /* independent baseline */
    bag_full_packet(12U,2U,600210U,37U,true);
    assert(!exp3_board_bag_full(600210U,&full));
    bag_full_packet(13U,2U,600300U,37U,true);
    assert(exp3_board_bag_full(600300U,&full) && !full);
    bag_full_packet(13U,3U,600310U,37U,true);
    assert(exp3_board_bag_full(600310U,&full) && full);
    bag_full_packet(13U,2U,600320U,37U,true);
    assert(exp3_board_bag_full(600320U,&full) && full); /* full wins */
    bag_full_packet(12U,2U,600330U,37U,true);
    assert(exp3_board_bag_full(600330U,&full) && full); /* older ignored */
    bag_full_packet(14U,2U,600340U,15U,true); /* truncated */
    bag_full_packet(14U,6U,600350U,37U,true); /* reserved flags */
    assert(exp3_board_bag_full(600350U,&full) && full);
    bag_full_packet(14U,2U,600400U,37U,true);
    assert(exp3_board_bag_full(600400U,&full) && !full); /* progress clears */
    bag_full_packet(14U,2U,601399U,37U,true);
    assert(exp3_board_bag_full(601399U,&full) && !full);
    assert(!exp3_board_bag_full(601400U,&full)); /* exact freshness */
    bag_full_packet(14U,3U,601400U,37U,true);
    assert(!exp3_board_bag_full(601400U,&full));
    bag_full_packet(15U,3U,601500U,37U,true);
    assert(exp3_board_bag_full(601500U,&full) && full);
    bag_full_packet(15U,0U,601510U,37U,false);
    assert(exp3_board_bag_connection(601510U,&connected) && !connected);
    assert(!exp3_board_bag_full(601510U,&full));
    bag_full_packet(15U,3U,601520U,37U,true);
    assert(!exp3_board_bag_full(601520U,&full)); /* delayed reconnect */
    bag_full_packet(16U,2U,601600U,37U,true);
    assert(!exp3_board_bag_full(601600U,&full));
    bag_full_packet(17U,2U,601700U,37U,true);
    assert(exp3_board_bag_full(601700U,&full) && !full);
    bag_full_packet(18U,0U,601800U,37U,true);
    assert(!exp3_board_bag_full(601800U,&full)); /* sample unavailable */
    bag_full_packet(19U,3U,601900U,37U,true);
    assert(!exp3_board_bag_full(601900U,&full));
    bag_full_packet(20U,3U,602000U,37U,true);
    assert(exp3_board_bag_full(602000U,&full) && full);
    bag_full_packet(16383U,2U,604000U,37U,true);
    assert(!exp3_board_bag_full(604000U,&full));
    bag_full_packet(0U,2U,604100U,37U,true);
    assert(exp3_board_bag_full(604100U,&full) && !full);
    bag_full_packet(10U,2U,0xfffffff0U,37U,true);
    assert(!exp3_board_bag_full(0xfffffff0U,&full));
    bag_full_packet(11U,3U,0x54U,37U,true);
    assert(exp3_board_bag_full(0x54U,&full) && full);
    g_exp3_hil.bound=0U;
    assert(!exp3_board_bag_full(0x54U,&full));
    g_exp3_hil.bound=1U;
    puts("PASS bag full four-byte AC, legacy unknown, full-wins, disconnect, replay/timeout/recovery, wraps");
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
    uint16_t mm, minimum, reasons;
    height_packet(0x10c6U,500000U,false,37U);
    height_packet(0x11c6U,500100U,false,37U);
    assert(exp3_board_height(500100U,&mm) && mm==70U);
    battery_packet(0x4000U|3350U,500000U,false,37U);
    battery_packet(0x6100U,500010U,false,37U);
    battery_packet(0x4000U|3350U,500090U,false,37U);
    battery_packet(0x6200U,500100U,false,37U);
    assert(exp3_board_battery(500100U,&minimum,&reasons) && minimum==3350U);
    /* Keep the old session's cached PD10 fresh while a new peer handshakes. */
    bag_full_packet(1024U,3U,500000U,37U,true);
    bag_full_packet(1025U,3U,500100U,37U,true);
    assert(exp3_board_bag_connection(500100U,&connected) && connected);
    bool full=false;
    assert(exp3_board_bag_full(500100U,&full) && full);
    exp3_auto_init(controller,EXP3_AUTO_CONTROLLER,0x87654323U,500100U);
    for (now=500100U; now<510100U; now+=10U) {
        uint16_t heartbeat=(uint16_t)(1025U+(now-500100U)/100U);
        bag_full_packet(heartbeat,3U,now,37U,true);
        run_pair(controller,now,now+10U);
        if (g_exp3_hil.controller != old_controller) break;
    }
    assert(now<510100U && g_exp3_hil.bound);
    assert(!exp3_board_bag_connection(now,&connected)); /* actual rebind cleared cache */
    assert(!exp3_board_bag_full(now,&full));
    assert(!exp3_board_height(now,&mm));
    assert(!exp3_board_battery(now,&minimum,&reasons));
    bag_packet(0x2003U,now+10U,false,37U);
    assert(!exp3_board_bag_connection(now+10U,&connected));
    bag_packet(0x2006U,now+110U,true,37U);
    assert(exp3_board_bag_connection(now+110U,&connected) && !connected);
    height_packet(0x20c6U,now+10U,false,37U);
    assert(!exp3_board_height(now+10U,&mm));
    height_packet(0x21aaU,now+110U,true,37U);
    assert(exp3_board_height(now+110U,&mm) && mm==42U);
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
        packet[0]=0xac;packet[11]=4U;packet[14]=packet[15]=0U; packet[9]=0xe2; packet[10]=10U;
        packet[12]=0x2f; packet[13]=0x20;
        exp3_board_rf(packet, sizeof(packet), 20000U);
        packet[13]=0x22; exp3_board_rf(packet, sizeof(packet), 20100U);
        assert(!exp3_board_runtime(20100U,&runtime)); /* incompatible profile */
        packet[9]=MOWER_FAST_STATUS_TYPE; packet[13]=0x20;
        exp3_board_rf(packet, sizeof(packet), 20200U);
        assert(!exp3_board_runtime(20200U,&runtime));
        packet[13]=0x22; exp3_board_rf(packet,sizeof(packet),20320U);
        assert(exp3_board_runtime(20320U,&runtime) && (runtime&7U)==7U);
        packet[12]=0x18; /* same heartbeat: lock/stop wins */
        exp3_board_rf(packet,sizeof(packet),20440U);
        assert(exp3_board_runtime(20440U,&runtime) && (runtime&MOWER_FAST_LOCKED) && !(runtime&7U));
        packet[12]=0x2f; exp3_board_rf(packet,sizeof(packet),20500U);
        assert(exp3_board_runtime(20500U,&runtime) && (runtime&MOWER_FAST_LOCKED) && !(runtime&7U));
        packet[12]=0x18; exp3_board_rf(packet,sizeof(packet),21400U);
        assert(!exp3_board_runtime(21400U,&runtime)); /* replay cannot renew */
        packet[12]=0x2b; packet[13]=0x20;
        exp3_board_rf(packet,sizeof(packet),300000U);
        assert(!exp3_board_runtime(300000U,&runtime));
        packet[13]=0x22; exp3_board_rf(packet,sizeof(packet),300120U);
        assert(exp3_board_runtime(300120U,&runtime) && (runtime&7U)==3U);
        packet[0]=0xac; packet[11]=4U; packet[12]=0x2d; packet[13]=0x24;
        exp3_board_rf(packet,13U,300200U);
        assert(exp3_board_runtime(300200U,&runtime) && (runtime&7U)==3U);
        exp3_board_rf(packet,sizeof(packet),300240U);
        assert(exp3_board_runtime(300240U,&runtime) && (runtime&7U)==5U);
    }
#endif
#if MOWER_BLADE_OK_ENABLED
    test_bag_connection();
    test_bag_full();
    test_height_feedback();
    test_battery_diagnostics();
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
        for (uint32_t t=318020U; t<320000U; t+=20U) exp3_board_poll(t);
        exp3_board_poll(320000U); /* Exact 10s absolute deadline, not per retry. */
        assert(!g_exp3_hil.bound && !g_exp3_hil.waiting && g_exp3_hil.cancel_failed);
        Exp3Link reply={0}; Exp3Message result=cancel;
        result.kind=EXP3_RESULT;result.sender=2U;result.command=EXP3_CANCEL;
        result.status=EXP3_CANCELLED;
        assert(exp3_link_send_result(&reply,&result,321000U));
        for(uint32_t t=321000U;reply.tx_active;t+=20U){
            uint16_t w=exp3_link_word(&reply,t,20U);
            if(w)exp3_hil_receive(&g_exp3_hil,NULL,w,t);
        }
        assert(!g_exp3_hil.bound && !g_exp3_hil.waiting); /* Old receipt cannot bind. */
        run_pair(&controller,330000U,340000U);
        assert(g_exp3_hil.bound && !g_exp3_hil.cancel_failed &&
               g_exp3_hil.hmi!=cancel.hmi && !g_exp3_hil.waiting);

    }
#if MOWER_BLADE_OK_ENABLED
    test_bag_rebind(&controller);
    test_completion_rejection();
#endif
    puts("PASS OpenI6X RF adapter auto binds and replaces rebooted controller session");
    return 0;
}
