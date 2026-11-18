#ifndef CAN_TX_SCHEDULER_H
#define CAN_TX_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_BLADE_TX_ATTEMPT_TIMEOUT_MS 20U
#define CAN_BLADE_COMMAND_MAX_AGE_MS 250U
/* Wheel/telemetry mailboxes: an unsent frame is stale; fresher intent replaces it. */
#define CAN_GENERAL_TX_ATTEMPT_TIMEOUT_MS 20U

typedef struct
{
    uint32_t stdId;
    uint32_t extId;
    uint8_t ide;
    uint8_t dlc;
    uint8_t rtr;
    uint8_t data[8];
    uint32_t queued_at_ms;
} CAN_TxMsg_t;

typedef struct
{
    bool stop_pending;
    bool mailbox_active;
    uint32_t stop_age_ms;
    uint32_t submit_failures;
    uint32_t transmit_failures;
    uint32_t retry_count;
    uint32_t abort_requests;
    uint32_t expired_commands;
    uint32_t stop_success_count;
    uint32_t last_success_ms;
} CAN_BladeTxDiagnostics_t;

/* Init once before tasks start. Process belongs exclusively to CANTx; it makes
 * one bounded step, returning true after a submission (otherwise delay 1ms).
 * Enqueue functions are nonblocking task-only interfaces, never ISR interfaces.
 * Blade intent has one producer (Motion); diagnostics may be read by any task.
 * Accepted blade commands remain owned here until completion or supersession.
 * Stop is sticky until its CAN TXOK, which does not certify physical stopping. */
void CAN_TxScheduler_Init(void);
bool CAN_TxScheduler_Process(void);
bool CAN_TxScheduler_ReadBladeDiagnostics(CAN_BladeTxDiagnostics_t *out);
uint8_t CAN_Enqueue_BladeControl_Message(uint32_t ext_id, const uint8_t *data,
                                       uint8_t len, bool is_stop);

uint8_t CAN_Enqueue_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote);
uint8_t CAN_Enqueue_Ext_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote);
uint8_t CAN_Enqueue_Control_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote);
uint8_t CAN_TryGet_Control_Message(CAN_TxMsg_t *msg);
uint8_t CAN_Enqueue_Telemetry_Message(uint32_t id, uint8_t *data, uint8_t len, uint8_t is_remote);
uint8_t CAN_TryGet_Telemetry_Message(CAN_TxMsg_t *msg);
uint8_t CAN_Enqueue_SpeedQuery(uint8_t node);
uint8_t CAN_TryGet_SpeedQuery(CAN_TxMsg_t *msg);
void CAN_Retry_TxMessage(const CAN_TxMsg_t *msg, uint8_t category);

extern volatile uint32_t g_can_tx_mailbox_failures;
extern volatile uint32_t g_can_tx_mailbox_busy;
extern volatile uint32_t g_can_speed_query_expired;
extern volatile uint32_t g_can_error_code;
extern volatile uint32_t g_can_tx_enqueue_failures;
/* Bus-off entries since boot; hardware rejoins automatically (ABOM). */
extern volatile uint32_t g_can_bus_off_events;
#endif
