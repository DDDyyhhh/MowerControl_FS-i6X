#include "can_tx_scheduler.h"
#include "can_tx_port.h"
#include "blade_motor_config.h"
#include "oid_can.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

volatile uint32_t g_can_tx_enqueue_failures = 0;
volatile uint32_t g_can_tx_mailbox_failures = 0;
volatile uint32_t g_can_tx_mailbox_busy = 0;
volatile uint32_t g_can_speed_query_expired = 0;
volatile uint32_t g_can_error_code = 0;
static CAN_TxMsg_t g_speed_queries[5];
static uint8_t g_speed_query_pending[5];
static uint8_t g_speed_query_cursor = 1U;
extern osMessageQueueId_t Queue_CanTxHandle;

static CAN_TxMsg_t g_latest_control[5];
static volatile uint8_t g_latest_control_pending[5] = {0};
static CAN_TxMsg_t g_latest_telemetry;
static volatile uint8_t g_latest_telemetry_pending = 0U;

typedef struct
{
    CAN_TxMsg_t msg;
    uint32_t generation;
    bool pending;
    bool stop;
    bool attempted;
    bool retry_wait;
    uint32_t retry_since_ms;
} CAN_BladeRequest_t;

typedef struct
{
    CAN_TxMsg_t msg;
    uint32_t generation;
    uint32_t submitted_at_ms;
    uint8_t category;
    bool active;
    bool stop;
    bool abort_requested;
} CAN_MailboxOwner_t;

static CAN_BladeRequest_t g_blade;
static CAN_MailboxOwner_t g_mailboxes[3];
static CAN_BladeTxDiagnostics_t g_blade_diagnostics;
/* A host build may seed near wrap to exercise generation equality. */
#ifndef CAN_TX_INITIAL_GENERATION
#define CAN_TX_INITIAL_GENERATION 0U
#endif
static uint32_t g_generation;

void CAN_TxScheduler_Init(void)
{
    memset(g_speed_queries, 0, sizeof(g_speed_queries));
    memset(g_speed_query_pending, 0, sizeof(g_speed_query_pending));
    memset(g_latest_control, 0, sizeof(g_latest_control));
    memset((void *)g_latest_control_pending, 0, sizeof(g_latest_control_pending));
    memset(&g_latest_telemetry, 0, sizeof(g_latest_telemetry));
    g_latest_telemetry_pending = 0U;
    g_speed_query_cursor = 1U;
    memset(&g_blade, 0, sizeof(g_blade));
    memset(g_mailboxes, 0, sizeof(g_mailboxes));
    memset(&g_blade_diagnostics, 0, sizeof(g_blade_diagnostics));
    g_generation = CAN_TX_INITIAL_GENERATION;
    g_can_tx_enqueue_failures = 0U;
    g_can_tx_mailbox_failures = 0U;
    g_can_tx_mailbox_busy = 0U;
    g_can_speed_query_expired = 0U;
    g_can_error_code = 0U;
}

static bool CAN_IsBladeControlId(uint32_t id)
{
    uint32_t command = id & 0xFFU;
    id &= 0x1FFFFFFFU;
    return (id >> 8) == FLIPSKY_ESC_ID &&
           (command == 0x18U || command == 0x02U ||
            command == FLIPSKY_CAN_CMD_CURRENT_PERCENT);
}

static void CAN_Build_Message(CAN_TxMsg_t *msg, uint32_t id, uint8_t ide,
                              const uint8_t *data, uint8_t len, uint8_t is_remote)
{
    memset(msg, 0, sizeof(*msg));
    msg->queued_at_ms = HAL_GetTick();
    msg->ide = ide;
    msg->stdId = (ide == CAN_ID_EXT) ? 0U : (id & 0x7FFU);
    msg->extId = (ide == CAN_ID_EXT) ? (id & 0x1FFFFFFFU) : 0U;
    msg->rtr = is_remote;
    msg->dlc = (len > 8U) ? 8U : len;

    if (data != NULL && !is_remote)
    {
        memcpy(msg->data, data, msg->dlc);
    }
}

/* Four latest-only slots keep feedback queries out of the ordinary FIFO.
 * Category order in CANTx is control, speed query, telemetry, ordinary FIFO. */
uint8_t CAN_Enqueue_SpeedQuery(uint8_t node)
{
    uint8_t data[2] = {0x0FU, 0x01U};
    CAN_TxMsg_t msg;
    if (node == 0U || node >= 5U) return 0U;
    CAN_Build_Message(&msg, node, CAN_ID_STD, data, 2U, 0U);
    taskENTER_CRITICAL();
    g_speed_queries[node] = msg;
    g_speed_query_pending[node] = 1U;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t CAN_TryGet_SpeedQuery(CAN_TxMsg_t *msg)
{
    uint32_t now;
    if (!msg) return 0U;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    for (unsigned i=0U; i<4U; ++i) {
        uint8_t node = g_speed_query_cursor;
        g_speed_query_cursor = node == 4U ? 1U : node + 1U;
        if (!g_speed_query_pending[node]) continue;
        g_speed_query_pending[node] = 0U;
        if ((uint32_t)(now-g_speed_queries[node].queued_at_ms) >= 50U) {
            ++g_can_speed_query_expired;
            continue;
        }
        *msg = g_speed_queries[node];
        taskEXIT_CRITICAL();
        return 1U;
    }
    taskEXIT_CRITICAL();
    return 0U;
}

void CAN_Retry_TxMessage(const CAN_TxMsg_t *msg, uint8_t category)
{
    uint8_t node = (uint8_t)msg->stdId;
    taskENTER_CRITICAL();
    if (node > 0U && node < 5U && category == 1U && !g_latest_control_pending[node]) {
        /* Never overwrite a newer stop/command produced during a failed send. */
        g_latest_control[node] = *msg;
        g_latest_control_pending[node] = 1U;
    } else if (node > 0U && node < 5U && category == 2U && !g_speed_query_pending[node] &&
               (uint32_t)(HAL_GetTick()-msg->queued_at_ms) < 50U) {
        g_speed_queries[node] = *msg;
        g_speed_query_pending[node] = 1U;
    }
    taskEXIT_CRITICAL();
}

static uint8_t CAN_Enqueue_MessageEx(uint32_t id,
                                     uint8_t ide,
                                     uint8_t *data,
                                     uint8_t len,
                                     uint8_t is_remote)
{
    CAN_TxMsg_t msg = {0};
    CAN_Build_Message(&msg, id, ide, data, len, is_remote);

    if (osMessageQueuePut(Queue_CanTxHandle, &msg, 0U, 0U) != osOK)
    {
        g_can_tx_enqueue_failures++;
        return 0U;
    }

    return 1U;
}

uint8_t CAN_Enqueue_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote)
{
    return CAN_Enqueue_MessageEx(id, CAN_ID_STD, data, len, is_remote);
}

uint8_t CAN_Enqueue_Ext_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote)
{
    if (CAN_IsBladeControlId(id)) {
        ++g_can_tx_enqueue_failures;
        return 0U;
    }
    return CAN_Enqueue_MessageEx(id, CAN_ID_EXT, data, len, is_remote);
}

uint8_t CAN_Enqueue_Control_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote)
{
    uint8_t node_id = (uint8_t)(id & 0xFFU);
    CAN_TxMsg_t msg;

    if (node_id == 0U || node_id >= 5U)
    {
        return 0U;
    }

    CAN_Build_Message(&msg, id, CAN_ID_STD, data, len, is_remote);

    taskENTER_CRITICAL();
    g_latest_control[node_id] = msg;
    g_latest_control_pending[node_id] = 1U;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t CAN_TryGet_Control_Message(CAN_TxMsg_t *msg)
{
    uint8_t node_id;

    if (msg == NULL)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    for (node_id = 1U; node_id < 5U; node_id++)
    {
        if (g_latest_control_pending[node_id] != 0U)
        {
            *msg = g_latest_control[node_id];
            g_latest_control_pending[node_id] = 0U;
            taskEXIT_CRITICAL();
            return 1U;
        }
    }
    taskEXIT_CRITICAL();
    return 0U;
}

uint8_t CAN_Enqueue_Telemetry_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote)
{
    CAN_TxMsg_t msg;
    uint8_t was_pending;

    CAN_Build_Message(&msg, id, CAN_ID_STD, data, len, is_remote);
    taskENTER_CRITICAL();
    was_pending = g_latest_telemetry_pending;
    g_latest_telemetry = msg;
    g_latest_telemetry_pending = 1U;
    taskEXIT_CRITICAL();
    return was_pending ? 0U : 1U;
}

uint8_t CAN_TryGet_Telemetry_Message(CAN_TxMsg_t *msg)
{
    if (msg == NULL)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    if (g_latest_telemetry_pending != 0U)
    {
        *msg = g_latest_telemetry;
        g_latest_telemetry_pending = 0U;
        taskEXIT_CRITICAL();
        return 1U;
    }
    taskEXIT_CRITICAL();
    return 0U;
}

uint8_t CAN_Enqueue_BladeControl_Message(uint32_t ext_id, const uint8_t *data,
                                       uint8_t len, bool is_stop)
{
    CAN_TxMsg_t msg;
    uint32_t command = ext_id & 0xFFU;
    uint8_t expected_length = command == 0x18U ? 5U : (command == 0x02U ? 7U : 6U);
    bool zero;
    if (!CAN_IsBladeControlId(ext_id) || !data || len != expected_length) return 0U;
    zero = (data[0] | data[1] | data[2] | data[3]) == 0U;
    if (zero != is_stop) return 0U;
    CAN_Build_Message(&msg, ext_id, CAN_ID_EXT, data, len, 0U);
    taskENTER_CRITICAL();
    if (g_blade.pending && g_blade.stop) {
        /* A repeated zero must not replace an in-flight stop's identity/age. */
        taskEXIT_CRITICAL();
        return is_stop ? 1U : 0U;
    }
    g_blade.msg = msg;
    g_blade.generation = ++g_generation;
    g_blade.pending = true;
    g_blade.stop = is_stop;
    g_blade.attempted = false;
    g_blade.retry_wait = false;
    taskEXIT_CRITICAL();
    return 1U;
}

bool CAN_TxScheduler_ReadBladeDiagnostics(CAN_BladeTxDiagnostics_t *out)
{
    if (!out) return false;
    taskENTER_CRITICAL();
    *out = g_blade_diagnostics;
    out->stop_pending = g_blade.pending && g_blade.stop;
    out->stop_age_ms = out->stop_pending ?
        (uint32_t)(HAL_GetTick() - g_blade.msg.queued_at_ms) : 0U;
    out->mailbox_active = g_mailboxes[2].active;
    taskEXIT_CRITICAL();
    return true;
}

static void CAN_SettleMailbox(uint8_t mailbox)
{
    CAN_TxPortResult_t result = CAN_TxPort_Poll(mailbox);
    CAN_MailboxOwner_t *owner = &g_mailboxes[mailbox];
    uint32_t now;
    if (result == CAN_TX_PENDING) return;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    if (owner->active) {
        if (result != CAN_TX_SUCCESS && result != CAN_TX_ABORTED) {
            ++g_can_tx_mailbox_failures;
        }
        if (mailbox == 2U) {
            if (result == CAN_TX_SUCCESS) {
                g_blade_diagnostics.last_success_ms = now;
                if (owner->stop) ++g_blade_diagnostics.stop_success_count;
                if (g_blade.pending && g_blade.generation == owner->generation) {
                    g_blade.pending = false;
                }
            } else {
                if (result != CAN_TX_ABORTED) ++g_blade_diagnostics.transmit_failures;
                if (g_blade.pending && g_blade.generation == owner->generation) {
                    g_blade.retry_wait = true;
                    g_blade.retry_since_ms = now;
                }
            }
        }
        owner->active = false;
    }
    taskEXIT_CRITICAL();
}

/* Caller holds the task critical section. Sample time after reading shared
 * intent so a preempting producer cannot publish a timestamp in our future. */
static void CAN_ExpireBladeRequest(uint32_t now)
{
    if (g_blade.pending && !g_blade.stop &&
        (uint32_t)(now - g_blade.msg.queued_at_ms) >= CAN_BLADE_COMMAND_MAX_AGE_MS) {
        g_blade.pending = false;
        ++g_blade_diagnostics.expired_commands;
    }
}

static void CAN_ServiceBladeAbort(void)
{
    CAN_MailboxOwner_t *owner = &g_mailboxes[2];
    uint32_t now;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    CAN_ExpireBladeRequest(now);
    if (owner->active && !owner->abort_requested &&
        ((!owner->stop && ((g_blade.pending && g_blade.stop) ||
           (uint32_t)(now - owner->msg.queued_at_ms) >= CAN_BLADE_COMMAND_MAX_AGE_MS)) ||
         (uint32_t)(now - owner->submitted_at_ms) > CAN_BLADE_TX_ATTEMPT_TIMEOUT_MS)) {
        CAN_TxPort_Abort(2U);
        owner->abort_requested = true;
        ++g_blade_diagnostics.abort_requests;
    }
    taskEXIT_CRITICAL();
}

static bool CAN_SubmitBlade(void)
{
    bool submitted = false;
    uint32_t now;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    /* Recheck immediately before submission: CANTx may have been preempted
     * since the abort/expiry phase, or a new producer may have replaced intent. */
    CAN_ExpireBladeRequest(now);
    if (g_blade.pending && !g_mailboxes[2].active && CAN_TxPort_IsEmpty(2U) &&
        (!g_blade.retry_wait || (uint32_t)(now - g_blade.retry_since_ms) >= 1U)) {
        CAN_MailboxOwner_t *owner = &g_mailboxes[2];
        if (g_blade.attempted) ++g_blade_diagnostics.retry_count;
        g_blade.attempted = true;
        if (CAN_TxPort_Submit(2U, &g_blade.msg)) {
            owner->msg = g_blade.msg;
            owner->generation = g_blade.generation;
            owner->submitted_at_ms = now;
            owner->category = 3U;
            owner->stop = g_blade.stop;
            owner->abort_requested = false;
            owner->active = true;
            submitted = true;
        } else {
            ++g_can_tx_mailbox_failures;
            ++g_blade_diagnostics.submit_failures;
            g_blade.retry_wait = true;
            g_blade.retry_since_ms = now;
        }
    }
    taskEXIT_CRITICAL();
    return submitted;
}

bool CAN_TxScheduler_Process(void)
{
    CAN_TxMsg_t msg;
    uint8_t mailbox, category = 0U;
    bool submitted;
    g_can_error_code = CAN_TxPort_GetError();
    for (mailbox = 0U; mailbox < 3U; ++mailbox) CAN_SettleMailbox(mailbox);
    CAN_ServiceBladeAbort();
    if (CAN_SubmitBlade()) return true;
    for (mailbox = 0U; mailbox < 2U; ++mailbox) {
        if (!g_mailboxes[mailbox].active && CAN_TxPort_IsEmpty(mailbox)) break;
    }
    if (mailbox == 2U) {
        ++g_can_tx_mailbox_busy;
        return false;
    }
    if (CAN_TryGet_Control_Message(&msg)) category = 1U;
    else if (CAN_TryGet_SpeedQuery(&msg)) category = 2U;
    else if (!CAN_TryGet_Telemetry_Message(&msg) &&
             osMessageQueueGet(Queue_CanTxHandle, &msg, NULL, 0U) != osOK) return false;
    submitted = CAN_TxPort_Submit(mailbox, &msg);
    if (submitted) {
        taskENTER_CRITICAL();
        g_mailboxes[mailbox].msg = msg;
        g_mailboxes[mailbox].generation = ++g_generation;
        g_mailboxes[mailbox].submitted_at_ms = HAL_GetTick();
        g_mailboxes[mailbox].category = category;
        g_mailboxes[mailbox].active = true;
        taskEXIT_CRITICAL();
    } else {
        ++g_can_tx_mailbox_failures;
        CAN_Retry_TxMessage(&msg, category);
    }
#if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    if (category == 2U && msg.stdId > 0U && msg.stdId < OID_MAX_NODES) {
        if (submitted) ++g_oid_drives[msg.stdId].speed_mailbox_ok;
        else ++g_oid_drives[msg.stdId].speed_mailbox_fail;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (submitted) ++g_oid_hil_diag[msg.stdId].mailbox_ok;
        else ++g_oid_hil_diag[msg.stdId].mailbox_fail;
#endif
    }
#endif
    return submitted;
}
