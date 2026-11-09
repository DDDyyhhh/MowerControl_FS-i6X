#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "can_tx_scheduler.h"
#include "can_tx_port.h"
#include "cmsis_os.h"
#include "oid_can.h"
#include "flipsky_can.h"
#include "BladeMotor.h"

osMessageQueueId_t Queue_CanTxHandle = 1U;
static uint32_t now_ms;
static unsigned critical_depth, attempts, aborts[3];
static bool full, fail_submit;
static CAN_TxMsg_t fifo[256], sent[1024];
static unsigned read_pos, write_pos;
static uint8_t sent_mailbox[1024];
static bool busy[3];
static CAN_TxPortResult_t results[3];
static void (*after_critical)(void);
static void (*on_poll)(void);
static bool inject_stop_on_blade_submit;
uint32_t HAL_GetTick(void) { return now_ms; }
void CanTxTest_EnterCritical(void) { ++critical_depth; }
void CanTxTest_ExitCritical(void)
{
    assert(critical_depth); --critical_depth;
    if (!critical_depth && after_critical) {
        void (*hook)(void) = after_critical; after_critical = NULL; hook();
    }
}
static void PublishStop(void) { assert(Flipsky_CAN_SetSpeedErpm(0.0f)); }
osStatus_t osMessageQueuePut(osMessageQueueId_t q, const void *p, uint8_t priority, uint32_t timeout)
{
    (void)q; (void)priority; assert(!timeout);
    if (full || write_pos - read_pos == 256U) return osError;
    fifo[write_pos++ % 256U] = *(const CAN_TxMsg_t *)p;
    return osOK;
}
osStatus_t osMessageQueueGet(osMessageQueueId_t q, void *p, uint8_t *priority, uint32_t timeout)
{
    (void)q; (void)priority; assert(!timeout);
    if (read_pos == write_pos) return osError;
    *(CAN_TxMsg_t *)p = fifo[read_pos++ % 256U];
    return osOK;
}
bool CAN_TxPort_IsEmpty(uint8_t m) { assert(m < 3U); return !busy[m]; }
bool CAN_TxPort_Submit(uint8_t m, const CAN_TxMsg_t *msg)
{
    assert(m < 3U && !busy[m]); assert(attempts < 1024U);
    if (m == 2U) assert(critical_depth);
    sent[attempts] = *msg; sent_mailbox[attempts++] = m;
    if (m == 2U && inject_stop_on_blade_submit) {
        inject_stop_on_blade_submit = false;
        after_critical = PublishStop; /* Task preemption is deferred by the lock. */
    }
    if (fail_submit) return false;
    busy[m] = true;
    return true;
}
CAN_TxPortResult_t CAN_TxPort_Poll(uint8_t m)
{
    if (m == 2U && on_poll) {
        void (*hook)(void) = on_poll; on_poll = NULL; hook();
    }
    CAN_TxPortResult_t r = results[m];
    if (r != CAN_TX_PENDING) { results[m] = CAN_TX_PENDING; busy[m] = false; }
    return r;
}
void CAN_TxPort_Abort(uint8_t m) { assert(m < 3U && busy[m]); ++aborts[m]; }
uint32_t CAN_TxPort_GetError(void) { return 0U; }
static bool bus_off;
bool CAN_TxPort_IsBusOff(void) { return bus_off; }
static void Reset(void)
{
    now_ms = 0; attempts = 0; full = false; fail_submit = false; bus_off = false;
    after_critical = NULL; on_poll = NULL; inject_stop_on_blade_submit = false;
    read_pos = write_pos = 0;
    memset(busy, 0, sizeof(busy)); memset(results, 0, sizeof(results));
    memset(aborts, 0, sizeof(aborts));
    CAN_TxScheduler_Init(); Flipsky_CAN_Init();
}
static bool Step(uint32_t now)
{
    now_ms = now;
    bool r = CAN_TxScheduler_Process();
    assert(!critical_depth);
    return r;
}
static CAN_BladeTxDiagnostics_t Diagnostics(void)
{
    CAN_BladeTxDiagnostics_t d;
    assert(CAN_TxScheduler_ReadBladeDiagnostics(&d));
    assert(!critical_depth); return d;
}
static void TestStopSurvivesFailures(void)
{
    Reset(); full = true;
    assert(Flipsky_CAN_SetSpeedErpm(0.0f));
    fail_submit = true; assert(!Step(0)); assert(attempts == 1);
    assert(!Step(0)); assert(attempts == 1);
    assert(!Step(1)); assert(attempts == 2);
    fail_submit = false; assert(Step(2)); assert(attempts == 3);
    assert(Diagnostics().stop_pending);
    results[2] = CAN_TX_ARBITRATION_LOST; assert(!Step(3));
    assert(Step(4)); results[2] = CAN_TX_TRANSMIT_ERROR; assert(!Step(5));
    assert(Step(6)); results[2] = CAN_TX_SUCCESS; assert(!Step(7));
    CAN_BladeTxDiagnostics_t d = Diagnostics();
    assert(!d.stop_pending && d.submit_failures == 2 && d.transmit_failures == 2);
    assert(d.retry_count == 4 && d.stop_success_count == 1 && d.last_success_ms == 7);
    for (unsigned i = 0; i < attempts; ++i) {
        assert(sent_mailbox[i] == 2 && sent[i].extId == 0x9a18 && sent[i].dlc == 5);
        assert(!sent[i].data[0] && !sent[i].data[1] && !sent[i].data[2] && !sent[i].data[3]);
    }
}
static void TestStopCancelsRunWithoutTouchingOtherMailboxes(void)
{
    uint8_t control[5] = {2, 0, 0, 1, 0};
    Reset();
    assert(CAN_Enqueue_Control_Message(1, control, 5, 0)); assert(Step(0));
    assert(OID_Req_Actual_Speed(2)); assert(Step(0));
    assert(sent_mailbox[0] == 0 && sent_mailbox[1] == 1);
    assert(Flipsky_CAN_SetSpeedErpm(14000)); assert(Step(1));
    assert(sent_mailbox[2] == 2);
    now_ms = 2; PublishStop();
    assert(!Flipsky_CAN_SetSpeedErpm(16000));
    Flipsky_CAN_Init(); /* Feedback init must not erase transport obligations. */
    assert(!Step(2));
    assert(aborts[2] == 1 && !aborts[0] && !aborts[1]);
    assert(attempts == 3 && Diagnostics().stop_pending);
    /* Abort may race a successful old run; settle it before sending zero. */
    results[2] = CAN_TX_SUCCESS;
    assert(Step(3)); assert(attempts == 4 && !sent[3].data[2]);
    assert(Diagnostics().stop_pending);
    now_ms = 4; PublishStop(); assert(Diagnostics().stop_age_ms == 2);
    assert(!Step(4) && attempts == 4);
    results[2] = CAN_TX_SUCCESS; assert(!Step(5));
    assert(!Diagnostics().stop_pending && attempts == 4);
    assert(!Step(6)); /* Rejected run was not cached for replay. */
    assert(Flipsky_CAN_SetSpeedErpm(16000)); assert(Step(7));
    assert(sent[4].data[2] == 0xc1 && sent[4].data[3] == 0x80);
}

static void TestOldResultCannotClearNewStop(void)
{
    Reset(); assert(Flipsky_CAN_SetSpeedErpm(14000)); assert(Step(0));
    results[2] = CAN_TX_SUCCESS; on_poll = PublishStop;
    assert(Step(1)); assert(attempts == 2 && !sent[1].data[2]);
    assert(Diagnostics().stop_pending);
    results[2] = CAN_TX_SUCCESS; assert(!Step(2));
    assert(!Diagnostics().stop_pending);

    Reset(); assert(Flipsky_CAN_SetSpeedErpm(14000));
    inject_stop_on_blade_submit = true; fail_submit = true;
    assert(!Step(0)); /* Deferred stop replaces the failed run after submission. */
    fail_submit = false; assert(Step(0));
    assert(attempts == 2 && !sent[1].data[2]);
    results[2] = CAN_TX_SUCCESS; assert(!Step(1));
    assert(!Diagnostics().stop_pending);
}

static void TestExpiryAndAttemptTimeout(void)
{
    Reset(); now_ms = UINT32_MAX - 10U;
    assert(Flipsky_CAN_SetSpeedErpm(14000)); fail_submit = true;
    assert(!Step(UINT32_MAX - 10U)); fail_submit = false;
    assert(Step(238)); /* age 249, including millisecond wrap */
    assert(!Step(239)); assert(aborts[2] == 1);
    assert(Diagnostics().expired_commands == 1);
    results[2] = CAN_TX_ABORTED; assert(!Step(240));
    assert(!Step(500) && attempts == 2);

    Reset(); assert(Flipsky_CAN_SetSpeedErpm(14000)); fail_submit = true;
    assert(!Step(0)); fail_submit = false;
    assert(!Step(250) && attempts == 1); /* Pending stale run never enters HW. */

    Reset(); now_ms = UINT32_MAX - 5U; PublishStop(); assert(Step(now_ms));
    assert(!Step(14) && !aborts[2]); /* exactly 20ms remains pending */
    assert(!Step(15) && aborts[2] == 1);
    assert(!Step(16) && aborts[2] == 1); /* no repeated abort storm */
    results[2] = CAN_TX_ABORTED; assert(!Step(17));
    assert(Step(18));
    assert(Diagnostics().stop_pending && Diagnostics().stop_age_ms == 24);
    results[2] = CAN_TX_SUCCESS; assert(!Step(19));
    assert(!Diagnostics().stop_pending);
}

static void TestLatestRunAndProtocolGuards(void)
{
    uint8_t zero[8] = {0};
    Reset();
    assert(!CAN_Enqueue_Ext_Message(0x9a18, zero, 5, 0));
    assert(!CAN_Enqueue_Ext_Message(0x9a02, zero, 7, 0));
    assert(!CAN_Enqueue_Ext_Message(0x9a03, zero, 6, 0));
    assert(!CAN_Enqueue_Ext_Message(0x20009a18, zero, 5, 0));
    assert(CAN_Enqueue_Ext_Message(0x9b18, zero, 5, 0));
    assert(!CAN_Enqueue_BladeControl_Message(0x9a18, zero, 4, true));
    assert(!CAN_Enqueue_BladeControl_Message(0x9a18, zero, 5, false));
    assert(Flipsky_CAN_SetSpeedErpm(14000));
    assert(Flipsky_CAN_SetSpeedErpm(16000)); assert(Step(0));
    assert(sent[0].data[2] == 0xc1 && sent[0].data[3] == 0x80);
    /* Newer run survives failure of the older mailbox generation. */
    assert(Flipsky_CAN_SetSpeedErpm(15500)); results[2] = CAN_TX_TRANSMIT_ERROR;
    assert(Step(1)); assert(sent[1].data[2] == 0xc3 && sent[1].data[3] == 0x74);
    results[2] = CAN_TX_SUCCESS; assert(Step(2)); /* unrelated FIFO uses mailbox 0 */
    assert(sent_mailbox[2] == 0 && sent[2].extId == 0x9b18);
    results[0] = CAN_TX_SUCCESS; assert(!Step(3));
    assert(Flipsky_CAN_SetCurrentPercent(50)); assert(Step(4));
    assert(sent[3].extId == 0x9a03 && sent[3].dlc == 6);
    results[2] = CAN_TX_SUCCESS; assert(!Step(5));
    assert(Flipsky_CAN_SetDutyCycle(50)); assert(Step(6));
    assert(sent[4].extId == 0x9a02 && sent[4].dlc == 7);
    assert(Flipsky_CAN_Stop()); assert(!Step(7)); assert(aborts[2] == 1);
    results[2] = CAN_TX_ABORTED; assert(Step(8));
    assert(sent[5].extId == 0x9a03 && !sent[5].data[2]);
}
static void AdvanceClock(void) { now_ms += 300U; }
static void QueueFreshRun(void) { now_ms = 100U; assert(Flipsky_CAN_SetSpeedErpm(14000)); }
static void TestClockSampleAfterPreemption(void)
{
    Reset(); assert(Flipsky_CAN_SetSpeedErpm(14000));
    after_critical = AdvanceClock; /* Delayed after expiry check, before submit. */
    assert(!Step(0) && attempts == 0);

    Reset(); after_critical = QueueFreshRun;
    assert(Step(0));
    assert(!Step(100) && !aborts[2]); /* Fresh publication cannot look 100ms old. */

    Reset(); PublishStop(); assert(Step(0));
    results[2] = CAN_TX_TRANSMIT_ERROR; on_poll = AdvanceClock;
    assert(!Step(1) && attempts == 1); /* Retry waits 1ms after observing failure. */
    assert(Step(302) && attempts == 2);
}
static void TestActualBladeMotorAndSafetyGate(void)
{
    SafetyOwnershipGateInput_t input = {false, false, false, true, true,
                                       EXECUTION_OWNER_MANUAL_REMOTE, 1U, 0U};
    Reset(); SafetyOwnershipGate_Reset(); SafetyOwnershipGate_Update(&input);
    BladeMotor_Start(); fail_submit = true; assert(!Step(0));
    assert(BladeMotor_SetSpeed(14000) == HAL_ERROR); /* pending startup zero */
    fail_submit = false; assert(Step(1));
    results[2] = CAN_TX_SUCCESS; assert(!Step(2));
    assert(!Step(3)); /* Hardware recovery alone cannot replay rejected start. */
    assert(BladeMotor_SetSpeed(14000) == HAL_OK); assert(Step(3));
    assert(sent[2].data[2] == 0xc9 && sent[2].data[3] == 0x50);
    input.software_locked = true; SafetyOwnershipGate_Update(&input);
    assert(BladeMotor_SetSpeed(16000) == HAL_OK); /* gate forces zero */
    assert(!Step(4) && aborts[2] == 1);
    results[2] = CAN_TX_SUCCESS; assert(Step(5));
    assert(!sent[3].data[2] && Diagnostics().stop_pending);
    assert(BladeMotor_SetSpeed(14000) == HAL_OK);
    results[2] = CAN_TX_SUCCESS; assert(!Step(6));
    assert(!Step(7) && attempts == 4);
}
static void TestBusOffRecovery(void)
{
    uint8_t data[8] = {0};
    /* A wheel frame stranded by bus-off is withdrawn, once, after its timeout. */
    Reset();
    assert(CAN_Enqueue_Message(0x123U, data, 8U, 0U));
    assert(Step(100)); assert(attempts == 1U && sent_mailbox[0] == 0U);
    (void)Step(100U + CAN_GENERAL_TX_ATTEMPT_TIMEOUT_MS); assert(aborts[0] == 0U);
    (void)Step(101U + CAN_GENERAL_TX_ATTEMPT_TIMEOUT_MS); assert(aborts[0] == 1U);
    (void)Step(150U); assert(aborts[0] == 1U);
    /* The mailbox is reusable afterwards and a fresh frame gets a fresh timeout. */
    results[0] = CAN_TX_ABORTED;
    assert(CAN_Enqueue_Message(0x124U, data, 8U, 0U));
    assert(Step(151U)); assert(attempts == 2U && sent_mailbox[1] == 0U && sent[1].stdId == 0x124U);
    (void)Step(151U + CAN_GENERAL_TX_ATTEMPT_TIMEOUT_MS); assert(aborts[0] == 1U);
    /* Bus-off entries are counted per edge. */
    Reset(); assert(g_can_bus_off_events == 0U);
    bus_off = true; (void)Step(1U); (void)Step(2U); assert(g_can_bus_off_events == 1U);
    bus_off = false; (void)Step(3U); assert(g_can_bus_off_events == 1U);
    bus_off = true; (void)Step(4U); assert(g_can_bus_off_events == 2U);
}

int main(void)
{
    TestBusOffRecovery();
    TestStopSurvivesFailures();
    TestStopCancelsRunWithoutTouchingOtherMailboxes();
    TestOldResultCannotClearNewStop();
    TestExpiryAndAttemptTimeout();
    TestLatestRunAndProtocolGuards();
    TestClockSampleAfterPreemption();
    TestActualBladeMotorAndSafetyGate();
    puts("PASS CAN blade stop: retained until TXOK, isolated mailbox, supersession, deferred preemption, abort/success race, expiry/clock/generation wrap and protocol guards");
}
