#include "sens_ibus_responder.h"

#include "../../../experiments/ticket03/exp3_board.h"
#include "../../../experiments/ticket03/bag_connection.h"
#include "../../../experiments/ticket03/height_feedback.h"
#include "../../../experiments/ticket03/battery_status.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    uint8_t sensor_type;
    TelemetryValueIndex_t snapshot_index;
} SensIbusSensorDefinition_t;

static const SensIbusSensorDefinition_t g_sensor_definitions[] = {
    {0x03U, TELEMETRY_VALUE_BATTERY_VOLTAGE},
    {0x05U, TELEMETRY_VALUE_BATTERY_CURRENT},
    {0x06U, TELEMETRY_VALUE_SOC},
    {0x13U, TELEMETRY_VALUE_GROUND_SPEED},
    {0x02U, TELEMETRY_VALUE_BLADE_RPM},
    {0x16U, TELEMETRY_VALUE_WORK_MODE},
    {0x15U, TELEMETRY_VALUE_RUN_PERMISSION},
    {0xE0U, TELEMETRY_VALUE_DETAILED_FAULT}
};

static uint16_t SensIbusResponder_Checksum(const uint8_t *bytes,
                                           uint8_t length_without_checksum)
{
    uint16_t checksum = 0xFFFFU;
    uint8_t i;

    for (i = 0U; i < length_without_checksum; i++)
    {
        checksum = (uint16_t)(checksum - bytes[i]);
    }
    return checksum;
}

static bool SensIbusResponder_RequestValid(const uint8_t request[4])
{
    uint16_t expected;
    uint16_t received;

    if (request[0] != SENS_IBUS_REQUEST_SIZE)
    {
        return false;
    }

    expected = SensIbusResponder_Checksum(request, 2U);
    received = (uint16_t)request[2] | ((uint16_t)request[3] << 8U);
    return received == expected;
}

static void SensIbusResponder_Finalize(uint8_t *response, uint8_t length)
{
    uint16_t checksum = SensIbusResponder_Checksum(
        response, (uint8_t)(length - 2U));
    response[length - 2U] = (uint8_t)(checksum & 0xFFU);
    response[length - 1U] = (uint8_t)(checksum >> 8U);
}

static void SensIbusResponder_Send(SensIbusResponder_t *responder,
                                   uint8_t *response,
                                   uint8_t length)
{
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    if (responder->observe != NULL)
    {
        responder->observe(responder->observe_context,
                           SENS_IBUS_RESPONDER_EVENT_RESPONSE_GENERATED,
                           responder->request);
    }
#endif
    SensIbusResponder_Finalize(response, length);
#if EXP3_ENABLED
    if ((responder->request[1] & 0x0FU) == 9U)
        exp3_board_sens(responder->request, response, length);
#endif
    if (responder->send_response != NULL)
    {
        (void)responder->send_response(responder->response_context,
                                       response, length);
    }
}

static bool SensIbusResponder_HandleRequest(SensIbusResponder_t *responder)
{
    uint8_t response[SENS_IBUS_MAX_RESPONSE_SIZE];
    uint8_t command;
    uint8_t address;
    uint8_t index;

    if (!SensIbusResponder_RequestValid(responder->request))
    {
        return false;
    }
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    if (responder->observe != NULL)
    {
        responder->observe(responder->observe_context,
                           SENS_IBUS_RESPONDER_EVENT_ACCEPTED,
                           responder->request);
    }
#endif

    command = responder->request[1] & 0xF0U;
    address = responder->request[1] & 0x0FU;
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED || EXP3_ENABLED
    if (address == 9U)
    {
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (responder->observe != NULL)
        {
            SensIbusResponderEvent_t route =
                SENS_IBUS_RESPONDER_EVENT_ROUTE_UNSUPPORTED_COMMAND;
            if (command == SENS_IBUS_COMMAND_DISCOVERY)
            {
                route = SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY;
            }
            else if (command == SENS_IBUS_COMMAND_TYPE)
            {
                route = SENS_IBUS_RESPONDER_EVENT_ROUTE_TYPE;
            }
            else if (command == SENS_IBUS_COMMAND_MEASUREMENT)
            {
                route = SENS_IBUS_RESPONDER_EVENT_ROUTE_MEASUREMENT;
            }
            responder->observe(responder->observe_context, route,
                               responder->request);
        }
#endif
        if (command == SENS_IBUS_COMMAND_DISCOVERY)
        {
            memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
            SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        }
        else if (command == SENS_IBUS_COMMAND_TYPE)
        {
            response[0] = SENS_IBUS_STANDARD_RESPONSE_SIZE;
            response[1] = responder->request[1];
            response[2] = 0xE1U; /* Unknown/raw sensor; not a blade message. */
            response[3] = 2U;
            SensIbusResponder_Send(responder, response,
                                   SENS_IBUS_STANDARD_RESPONSE_SIZE);
        }
        else if (command == SENS_IBUS_COMMAND_MEASUREMENT)
        {
            response[0] = SENS_IBUS_STANDARD_RESPONSE_SIZE;
            response[1] = responder->request[1];
#if EXP3_ENABLED
            uint16_t word = g_exp3_downlink_word;
#if MOWER_BLADE_OK_ENABLED
            /* Lowest priority: no extra SENS address, no receipt preemption. */
            TelemetrySnapshot_t snapshot;
            if (!word && responder->read_snapshot != NULL &&
                responder->read_snapshot(responder->snapshot_context, &snapshot))
                word = mower_battery_diag_word(
                    snapshot.values[TELEMETRY_VALUE_BATTERY_MIN_CELL],
                    snapshot.values[TELEMETRY_VALUE_BATTERY_REASON], snapshot.generated_at_ms);
#endif
            response[2] = (uint8_t)word;
            response[3] = (uint8_t)(word >> 8);
#else
            response[2] = 0x9CU;
            response[3] = 0xA5U; /* Raw test value 0xA59C. */
#endif
            SensIbusResponder_Send(responder, response,
                                   SENS_IBUS_STANDARD_RESPONSE_SIZE);
        }
        return true;
    }
#endif
#if MOWER_BLADE_OK_ENABLED
    if (address >= EXP3_COMPLETION_ADDRESS &&
        address < EXP3_COMPLETION_ADDRESS + EXP3_COMPLETION_PARTS) {
        unsigned part = address - EXP3_COMPLETION_ADDRESS;
        if (command == SENS_IBUS_COMMAND_DISCOVERY) {
            memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
            SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        } else if (command == SENS_IBUS_COMMAND_TYPE) {
            response[0]=6U; response[1]=responder->request[1];
            response[2]=(uint8_t)(EXP3_COMPLETION_TYPE+part); response[3]=4U;
            SensIbusResponder_Send(responder, response, 6U);
        } else if (command == SENS_IBUS_COMMAND_MEASUREMENT) {
            uint32_t word = exp3_board_completion_part(part);
            response[0]=8U; response[1]=responder->request[1];
            for (unsigned i=0U;i<4U;++i) response[2U+i]=(uint8_t)(word>>(i*8U));
            SensIbusResponder_Send(responder, response, 8U);
        }
        return true;
    }
    /* Dedicated read-only runtime sensor. Existing addresses 1..8 are unchanged. */
    if (address == 10U) {
        if (command == SENS_IBUS_COMMAND_DISCOVERY) {
            memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
            SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        } else if (command == SENS_IBUS_COMMAND_TYPE) {
            response[0]=6U; response[1]=responder->request[1];
            response[2]=MOWER_FAST_STATUS_TYPE; response[3]=4U;
            SensIbusResponder_Send(responder, response, 6U);
        } else if (command == SENS_IBUS_COMMAND_MEASUREMENT) {
            uint32_t value = TelemetrySnapshot_ReadFastAuthorization();
            response[0]=8U; response[1]=responder->request[1];
            for (unsigned i=0U;i<4U;++i) response[2U+i]=(uint8_t)(value>>(8U*i));
            SensIbusResponder_Send(responder, response, 8U);
        }
        return true;
    }
#endif
#if MOWER_BLADE_OK_ENABLED
    if (address == MOWER_HEIGHT_SENS_ADDRESS) {
        if (command == SENS_IBUS_COMMAND_DISCOVERY) {
            memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
            SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        } else if (command == SENS_IBUS_COMMAND_TYPE || command == SENS_IBUS_COMMAND_MEASUREMENT) {
            TelemetrySnapshot_t snapshot;
            uint16_t value = 0U;
            if (command == SENS_IBUS_COMMAND_TYPE)
                value = (uint16_t)(0x0200U | MOWER_HEIGHT_SENS_TYPE);
            else if (responder->read_snapshot != NULL &&
                     responder->read_snapshot(responder->snapshot_context, &snapshot))
                value = snapshot.values[TELEMETRY_VALUE_HEIGHT_FEEDBACK];
            response[0]=6U; response[1]=responder->request[1];
            response[2]=(uint8_t)value; response[3]=(uint8_t)(value>>8);
            SensIbusResponder_Send(responder, response, 6U);
        }
        return true;
    }
#endif
#if MOWER_GRASS_BAG_CH6_ENABLED
    if (address == MOWER_BAG_SENS_ADDRESS) {
        if (command == SENS_IBUS_COMMAND_DISCOVERY) {
            memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
            SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        } else if (command == SENS_IBUS_COMMAND_TYPE) {
            response[0]=6U; response[1]=responder->request[1];
            response[2]=MOWER_BAG_SENS_TYPE; response[3]=4U;
            SensIbusResponder_Send(responder, response, 6U);
        } else if (command == SENS_IBUS_COMMAND_MEASUREMENT) {
            TelemetrySnapshot_t snapshot;
            uint32_t value = 0U; /* No snapshot means unknown, never disconnected. */
            if (responder->read_snapshot != NULL &&
                     responder->read_snapshot(responder->snapshot_context, &snapshot))
                value = snapshot.values[TELEMETRY_VALUE_BAG_CONNECTION] |
                    ((uint32_t)snapshot.values[TELEMETRY_VALUE_BAG_FULL] << 16);
            response[0]=8U; response[1]=responder->request[1];
            for (unsigned i=0U; i<4U; ++i) response[2U+i]=(uint8_t)(value>>(8U*i));
            SensIbusResponder_Send(responder, response, 8U);
        }
        return true;
    }
#endif
    if ((address < SENS_IBUS_FIRST_ADDRESS) ||
        (address > SENS_IBUS_LAST_ADDRESS))
    {
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (responder->observe != NULL)
        {
            responder->observe(responder->observe_context,
                               SENS_IBUS_RESPONDER_EVENT_ROUTE_ABSENT_ADDRESS,
                               responder->request);
        }
#endif
        return true;
    }

    index = (uint8_t)(address - SENS_IBUS_FIRST_ADDRESS);
    if (command == SENS_IBUS_COMMAND_DISCOVERY)
    {
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (responder->observe != NULL)
        {
            responder->observe(responder->observe_context,
                               SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY,
                               responder->request);
        }
#endif
        memcpy(response, responder->request, SENS_IBUS_REQUEST_SIZE);
        SensIbusResponder_Send(responder, response, SENS_IBUS_REQUEST_SIZE);
        return true;
    }

    if (command == SENS_IBUS_COMMAND_TYPE)
    {
        response[0] = SENS_IBUS_STANDARD_RESPONSE_SIZE;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (responder->observe != NULL)
        {
            responder->observe(responder->observe_context,
                               SENS_IBUS_RESPONDER_EVENT_ROUTE_TYPE,
                               responder->request);
        }
#endif
        response[1] = responder->request[1];
        response[2] = g_sensor_definitions[index].sensor_type;
        response[3] = 2U;
        SensIbusResponder_Send(responder, response,
                               SENS_IBUS_STANDARD_RESPONSE_SIZE);
        return true;
    }

    if (command == SENS_IBUS_COMMAND_MEASUREMENT)
    {
        TelemetrySnapshot_t snapshot;
        uint16_t value;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
        if (responder->observe != NULL)
        {
            responder->observe(responder->observe_context,
                               SENS_IBUS_RESPONDER_EVENT_ROUTE_MEASUREMENT,
                               responder->request);
        }
#endif

        if ((responder->read_snapshot == NULL) ||
            !responder->read_snapshot(responder->snapshot_context, &snapshot))
        {
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
            if (responder->observe != NULL)
            {
                responder->observe(
                    responder->observe_context,
                    SENS_IBUS_RESPONDER_EVENT_ROUTE_SNAPSHOT_UNAVAILABLE,
                    responder->request);
            }
#endif
            return true;
        }

        value = snapshot.values[g_sensor_definitions[index].snapshot_index];
        response[0] = SENS_IBUS_STANDARD_RESPONSE_SIZE;
        response[1] = responder->request[1];
        response[2] = (uint8_t)(value & 0xFFU);
        response[3] = (uint8_t)(value >> 8U);
        SensIbusResponder_Send(responder, response,
                               SENS_IBUS_STANDARD_RESPONSE_SIZE);
    }
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    if ((command != SENS_IBUS_COMMAND_DISCOVERY) &&
        (command != SENS_IBUS_COMMAND_TYPE) &&
        (command != SENS_IBUS_COMMAND_MEASUREMENT) &&
        (responder->observe != NULL))
    {
        responder->observe(
            responder->observe_context,
            SENS_IBUS_RESPONDER_EVENT_ROUTE_UNSUPPORTED_COMMAND,
            responder->request);
    }
#endif
    return true;
}

static void SensIbusResponder_KeepCandidateSuffix(SensIbusResponder_t *responder)
{
    uint8_t start;

    for (start = 1U; start < SENS_IBUS_REQUEST_SIZE; start++)
    {
        if (responder->request[start] == SENS_IBUS_REQUEST_SIZE)
        {
            responder->received = (uint8_t)(SENS_IBUS_REQUEST_SIZE - start);
            memmove(responder->request, &responder->request[start],
                    responder->received);
            return;
        }
    }
    responder->received = 0U;
}

void SensIbusResponder_Init(SensIbusResponder_t *responder,
                            SensIbusSnapshotReadFn read_snapshot,
                            void *snapshot_context,
                            SensIbusResponseFn send_response,
                            void *response_context)
{
    if (responder == NULL)
    {
        return;
    }

    memset(responder, 0, sizeof(*responder));
    responder->read_snapshot = read_snapshot;
    responder->snapshot_context = snapshot_context;
    responder->send_response = send_response;
    responder->response_context = response_context;
}
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
void SensIbusResponder_SetObserver(SensIbusResponder_t *responder,
                                   SensIbusResponderObserveFn observe,
                                   void *observe_context)
{
    if (responder != NULL)
    {
        responder->observe = observe;
        responder->observe_context = observe_context;
    }
}
#endif


void SensIbusResponder_ResetStream(SensIbusResponder_t *responder)
{
    if (responder != NULL)
    {
        responder->received = 0U;
    }
}

void SensIbusResponder_Feed(SensIbusResponder_t *responder,
                            const uint8_t *bytes,
                            uint16_t length)
{
    uint16_t i;

    if ((responder == NULL) || (bytes == NULL))
    {
        return;
    }

    for (i = 0U; i < length; i++)
    {
        if ((responder->received == 0U) &&
            (bytes[i] != SENS_IBUS_REQUEST_SIZE))
        {
            continue;
        }

        responder->request[responder->received++] = bytes[i];
        if (responder->received == SENS_IBUS_REQUEST_SIZE)
        {
            if (SensIbusResponder_HandleRequest(responder))
            {
                responder->received = 0U;
            }
            else
            {
                SensIbusResponder_KeepCandidateSuffix(responder);
            }
        }
    }
}
