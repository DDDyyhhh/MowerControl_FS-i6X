#ifndef SENS_IBUS_RESPONDER_H
#define SENS_IBUS_RESPONDER_H

#include <stdbool.h>
#include <stdint.h>

#include "telemetry_snapshot.h"

#define SENS_IBUS_REQUEST_SIZE       4U
#define SENS_IBUS_MAX_RESPONSE_SIZE  6U
#define SENS_IBUS_FIRST_ADDRESS      1U
#define SENS_IBUS_LAST_ADDRESS       8U

#define SENS_IBUS_COMMAND_DISCOVERY   0x80U
#define SENS_IBUS_COMMAND_TYPE        0x90U
#define SENS_IBUS_COMMAND_MEASUREMENT 0xA0U
/* Isolated receiver discovery probe only; never enable in production. */
#ifndef SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
#define SENS_IBUS_HIL_ADDR9_PROBE_ENABLED 0
#endif
#if ((SENS_IBUS_HIL_ADDR9_PROBE_ENABLED != 0) && \
     (SENS_IBUS_HIL_ADDR9_PROBE_ENABLED != 1))
#error "SENS_IBUS_HIL_ADDR9_PROBE_ENABLED must be 0 or 1"
#endif
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED && !SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
#error "Address 9 probe requires isolated HIL diagnostics"
#endif

typedef bool (*SensIbusSnapshotReadFn)(void *context,
                                       TelemetrySnapshot_t *snapshot);
typedef bool (*SensIbusResponseFn)(void *context,
                                   const uint8_t *response,
                                   uint8_t length);

#ifndef SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
#define SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED 0
#endif

#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
typedef enum
{
    SENS_IBUS_RESPONDER_EVENT_ACCEPTED = 0,
    SENS_IBUS_RESPONDER_EVENT_RESPONSE_GENERATED,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_TYPE,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_MEASUREMENT,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_ABSENT_ADDRESS,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_UNSUPPORTED_COMMAND,
    SENS_IBUS_RESPONDER_EVENT_ROUTE_SNAPSHOT_UNAVAILABLE
} SensIbusResponderEvent_t;

typedef void (*SensIbusResponderObserveFn)(
    void *context, SensIbusResponderEvent_t event,
    const uint8_t request[SENS_IBUS_REQUEST_SIZE]);
#endif
typedef struct
{
    uint8_t request[SENS_IBUS_REQUEST_SIZE];
    uint8_t received;
    SensIbusSnapshotReadFn read_snapshot;
    void *snapshot_context;
    SensIbusResponseFn send_response;
    void *response_context;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    SensIbusResponderObserveFn observe;
    void *observe_context;
#endif
} SensIbusResponder_t;

void SensIbusResponder_Init(SensIbusResponder_t *responder,
                            SensIbusSnapshotReadFn read_snapshot,
                            void *snapshot_context,
                            SensIbusResponseFn send_response,
                            void *response_context);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
void SensIbusResponder_SetObserver(SensIbusResponder_t *responder,
                                   SensIbusResponderObserveFn observe,
                                   void *observe_context);
#endif
void SensIbusResponder_ResetStream(SensIbusResponder_t *responder);
void SensIbusResponder_Feed(SensIbusResponder_t *responder,
                            const uint8_t *bytes,
                            uint16_t length);

#endif
