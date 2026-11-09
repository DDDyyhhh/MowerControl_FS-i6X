#include "sens_ibus_usart2.h"
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "chassis_control.h"
#include "remote_backend.h"
#if EXP3_PWM_PROBE_ENABLED
#include "../../../experiments/ticket03/exp3_board.h"
#endif
#endif

#include "cmsis_os2.h"
#include "remote_backend_config.h"
#include "sens_ibus_responder.h"
#include "telemetry_snapshot.h"
#include "usart.h"

#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED
static SensIbusResponder_t g_responder;
static TelemetrySnapshot_t g_last_snapshot;

#if SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#define SENS_IBUS_TASK_FLAG_REQUEST 1U

static volatile uint8_t g_request_mailbox[SENS_IBUS_REQUEST_SIZE];
static volatile uint8_t g_request_ready;
static uint8_t g_rx_byte;
static uint8_t g_rx_count;
static uint8_t g_tx_buffer[SENS_IBUS_MAX_RESPONSE_SIZE];
static volatile uint8_t g_tx_busy;
static osThreadId_t g_task;
static const osThreadAttr_t g_task_attributes = {
    .name = "Task_SensIbus",
    .stack_size = 256U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal
};
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
static SensIbusHilDiag_t g_hil_diag;
static const osThreadAttr_t g_diag_task_attributes = {
    .name = "Task_SensDiag",
    .stack_size = 384U * 4U,
    .priority = (osPriority_t)osPriorityLow
};

#define SENS_IBUS_DIAG_INC(field) (g_hil_diag.field++)
static volatile uint32_t g_source_diag_sequence;
static volatile SensIbusTelemetrySourceDiag_t g_source_diag;
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
/* HIL only: valid requests and completed UART transmissions, not RF delivery. */
typedef struct
{
    volatile uint32_t req[3];
    volatile uint32_t rsp[3];
    volatile uint32_t last_req_ms;
    volatile uint32_t last_rsp_ms;
    volatile uint32_t txfail;
    volatile uint32_t err;
    volatile uint8_t first_req[3][SENS_IBUS_REQUEST_SIZE];
    volatile uint8_t first_rsp[3][SENS_IBUS_MAX_RESPONSE_SIZE];
} SensIbusAddr9Diag_t;
static SensIbusAddr9Diag_t g_sens9;
static volatile uint8_t g_sens9_tx_kind; /* 0=none; 1=DISC, 2=TYPE, 3=MEAS */

static uint8_t SensIbusUsart2_Addr9Kind(uint8_t command)
{
    if (command == SENS_IBUS_COMMAND_DISCOVERY) return 1U;
    if (command == SENS_IBUS_COMMAND_TYPE) return 2U;
    if (command == SENS_IBUS_COMMAND_MEASUREMENT) return 3U;
    return 0U;
}

static void SensIbusUsart2_ObserveAddr9(
    const uint8_t request[SENS_IBUS_REQUEST_SIZE])
{
    uint8_t kind;
    uint8_t i;
    if ((request[1] & 0x0FU) != 9U) return;
    kind = SensIbusUsart2_Addr9Kind(request[1] & 0xF0U);
    if (kind == 0U) return;
    kind--;
    if (g_sens9.req[kind] == 0U)
    {
        for (i = 0U; i < SENS_IBUS_REQUEST_SIZE; i++)
            g_sens9.first_req[kind][i] = request[i];
    }
    g_sens9.last_req_ms = HAL_GetTick();
    g_sens9.req[kind]++;
}

static void SensIbusUsart2_Addr9TxComplete(void)
{
    uint8_t index;
    uint8_t i;
    uint8_t size;
    if (g_sens9_tx_kind == 0U) return;
    index = (uint8_t)(g_sens9_tx_kind - 1U);
    if (g_sens9.rsp[index] == 0U)
    {
        size = (index == 0U) ?
            SENS_IBUS_REQUEST_SIZE : SENS_IBUS_MAX_RESPONSE_SIZE;
        for (i = 0U; i < size; i++)
            g_sens9.first_rsp[index][i] = g_tx_buffer[i];
    }
    g_sens9.last_rsp_ms = HAL_GetTick();
    g_sens9.rsp[index]++;
    g_sens9_tx_kind = 0U;
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SENS_IBUS_COMPILER_BARRIER() __asm volatile ("" ::: "memory")
#else
#define SENS_IBUS_COMPILER_BARRIER() ((void)0)
#endif

#else
#define SENS_IBUS_DIAG_INC(field) ((void)0)
#endif
#endif
#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED && \
    !SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH
#define SENS_IBUS_SCOPE_MARKER_HIGH() \
    HAL_GPIO_WritePin(SENS_IBUS_HIL_SCOPE_MARKER_PORT, \
                      SENS_IBUS_HIL_SCOPE_MARKER_PIN, GPIO_PIN_SET)
#define SENS_IBUS_SCOPE_MARKER_LOW() \
    HAL_GPIO_WritePin(SENS_IBUS_HIL_SCOPE_MARKER_PORT, \
                      SENS_IBUS_HIL_SCOPE_MARKER_PIN, GPIO_PIN_RESET)
#else
#define SENS_IBUS_SCOPE_MARKER_HIGH() ((void)0)
#define SENS_IBUS_SCOPE_MARKER_LOW()  ((void)0)
#endif
#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED && \
    !SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH
static void SensIbusUsart2_RunScopeMarkerBootSelfTest(void)
{
    SENS_IBUS_SCOPE_MARKER_LOW();
    SENS_IBUS_SCOPE_MARKER_HIGH();
    (void)osDelay(100U);
    SENS_IBUS_SCOPE_MARKER_LOW();
    (void)osDelay(100U);
    SENS_IBUS_SCOPE_MARKER_HIGH();
    (void)osDelay(100U);
    SENS_IBUS_SCOPE_MARKER_LOW();
}
#endif
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
static void SensIbusUsart2_ObserveResponder(
    void *context, SensIbusResponderEvent_t event,
    const uint8_t request[SENS_IBUS_REQUEST_SIZE])
{
    uint8_t i;

    (void)context;
    for (i = 0U; i < SENS_IBUS_REQUEST_SIZE; i++)
    {
        g_hil_diag.last_frame[i] = request[i];
    }
    g_hil_diag.last_raw_cmd_addr = request[1];
    g_hil_diag.last_command_class = request[1] & 0xF0U;
    g_hil_diag.last_address = request[1] & 0x0FU;
    if (event >= SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY)
    {
        g_hil_diag.last_route_result = (uint8_t)event;
    }
    if (event == SENS_IBUS_RESPONDER_EVENT_ACCEPTED)
    {
        SENS_IBUS_DIAG_INC(responder_accept_count);
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        SensIbusUsart2_ObserveAddr9(request);
#endif
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_RESPONSE_GENERATED)
    {
        SENS_IBUS_DIAG_INC(response_generated_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY)
    {
        SENS_IBUS_DIAG_INC(route_discovery_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_TYPE)
    {
        SENS_IBUS_DIAG_INC(route_type_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_MEASUREMENT)
    {
        SENS_IBUS_DIAG_INC(route_measurement_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_ABSENT_ADDRESS)
    {
        SENS_IBUS_DIAG_INC(route_absent_address_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_UNSUPPORTED_COMMAND)
    {
        SENS_IBUS_DIAG_INC(route_unsupported_command_count);
    }
    else if (event == SENS_IBUS_RESPONDER_EVENT_ROUTE_SNAPSHOT_UNAVAILABLE)
    {
        SENS_IBUS_DIAG_INC(route_snapshot_unavailable_count);
    }
}
static const char *SensIbusUsart2_RouteName(uint8_t route)
{
    switch ((SensIbusResponderEvent_t)route)
    {
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_DISCOVERY:
        return "DISCOVERY_SENSOR_FOUND";
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_TYPE:
        return "TYPE_SENSOR_FOUND";
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_MEASUREMENT:
        return "MEASUREMENT_SENSOR_FOUND";
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_ABSENT_ADDRESS:
        return "ABSENT_ADDRESS";
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_UNSUPPORTED_COMMAND:
        return "UNSUPPORTED_COMMAND";
    case SENS_IBUS_RESPONDER_EVENT_ROUTE_SNAPSHOT_UNAVAILABLE:
        return "SNAPSHOT_UNAVAILABLE";
    default:
        return "NONE";
    }
}

static void SensIbusUsart2_ReadTelemetrySourceDiag(
    SensIbusTelemetrySourceDiag_t *source)
{
    uint32_t sequence_before;
    uint32_t sequence_after;
    uint8_t i;

    do
    {
        sequence_before = g_source_diag_sequence;
        if ((sequence_before & 1U) != 0U)
        {
            continue;
        }
        SENS_IBUS_COMPILER_BARRIER();
        source->bms_voltage_centivolts = g_source_diag.bms_voltage_centivolts;
        source->bms_current_deciamps = g_source_diag.bms_current_deciamps;
        source->bms_soc_percent = g_source_diag.bms_soc_percent;
        for (i = 0U; i < 4U; i++)
        {
            source->wheel_mm_per_s[i] = g_source_diag.wheel_mm_per_s[i];
        }
        source->ground_speed_cm_per_s = g_source_diag.ground_speed_cm_per_s;
        source->blade_rpm = g_source_diag.blade_rpm;
        source->work_mode = g_source_diag.work_mode;
        source->run_permission = g_source_diag.run_permission;
        source->fault_bitmap = g_source_diag.fault_bitmap;
        SENS_IBUS_COMPILER_BARRIER();
        sequence_after = g_source_diag_sequence;
    } while ((sequence_before != sequence_after) ||
             ((sequence_after & 1U) != 0U));
}

#if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
static void SensIbusUsart2_PrintOidDiag(uint8_t node, char *line, size_t capacity)
{
    OID_HilDiag_t diag;
    uint32_t sampled_at_ms;
    int length;

    vTaskSuspendAll();
    diag = g_oid_hil_diag[node];
    sampled_at_ms = HAL_GetTick();
    (void)xTaskResumeAll();

    length = snprintf(
        line, capacity,
        "OID n=%u q_try=%lu q_queued=%lu mb_ok=%lu mb_fail=%lu "
        "speed_rx=%lu last_ms=%lu id=%03lX dlc=%u "
        "data=%02X,%02X,%02X,%02X,%02X,%02X sampled_at_ms=%lu\r\n",
        (unsigned int)node, (unsigned long)diag.query_attempts,
        (unsigned long)diag.query_queued, (unsigned long)diag.mailbox_ok,
        (unsigned long)diag.mailbox_fail,
        (unsigned long)diag.speed_response.count,
        (unsigned long)diag.speed_response.tick,
        (unsigned long)diag.speed_response.can_id,
        (unsigned int)diag.speed_response.dlc,
        diag.speed_response.data[0], diag.speed_response.data[1],
        diag.speed_response.data[2], diag.speed_response.data[3],
        diag.speed_response.data[4], diag.speed_response.data[5],
        (unsigned long)sampled_at_ms);
    if ((length > 0) && ((size_t)length < capacity))
    {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                (uint16_t)length, 100U);
    }
    length = snprintf(
        line, capacity,
        "OIDRX n=%u rx=%lu last_ms=%lu id=%03lX dlc=%u "
        "data=%02X,%02X,%02X,%02X,%02X,%02X sampled_at_ms=%lu\r\n",
        (unsigned int)node, (unsigned long)diag.any_response.count,
        (unsigned long)diag.any_response.tick,
        (unsigned long)diag.any_response.can_id,
        (unsigned int)diag.any_response.dlc,
        diag.any_response.data[0], diag.any_response.data[1],
        diag.any_response.data[2], diag.any_response.data[3],
        diag.any_response.data[4], diag.any_response.data[5],
        (unsigned long)sampled_at_ms);
    if ((length > 0) && ((size_t)length < capacity))
    {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                (uint16_t)length, 100U);
    }
}

static void SensIbusUsart2_PrintWheelDiag(char *line, size_t capacity)
{
    static const uint8_t nodes[4] = {
        OID_NODE_FL, OID_NODE_FR, OID_NODE_RL, OID_NODE_RR
    };
    ChassisWheelFeedback_t wheels[4];
    int32_t raw_erpm[4];
    uint32_t sampled_at_ms;
    uint8_t i;
    uint8_t node;

    /* OID feedback is written by Task_CANRx. Keep its fields and the existing
     * speed conversion coherent without masking interrupts or blocking on UART.
     */
    vTaskSuspendAll();
    (void)chassis_control_read_wheel_feedback(wheels);
    for (i = 0U; i < 4U; i++)
    {
        raw_erpm[i] = g_oid_drives[nodes[i]].actual_speed_erpm;
    }
    sampled_at_ms = HAL_GetTick();
    (void)xTaskResumeAll();

    for (node = 1U; node <= 4U; node++)
    {
        for (i = 0U; i < 4U; i++)
        {
            int length;

            if (nodes[i] != node)
            {
                continue;
            }
            length = snprintf(
                line, capacity,
                "WHEEL n=%u valid=%u age_ms=%lu erpm=%ld speed=%ld "
                "actual_speed_update_tick=%lu sampled_at_ms=%lu\r\n",
                (unsigned int)node, (unsigned int)wheels[i].valid,
                (unsigned long)(uint32_t)(sampled_at_ms - wheels[i].last_good_tick),
                (long)raw_erpm[i], (long)wheels[i].speed_mm_per_s,
                (unsigned long)wheels[i].last_good_tick,
                (unsigned long)sampled_at_ms);
            if ((length > 0) && ((size_t)length < capacity))
            {
                (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                        (uint16_t)length, 100U);
            }
            SensIbusUsart2_PrintOidDiag(node, line, capacity);
            break;
        }
    }
}
#endif
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
static void SensIbusUsart2_PrintAddr9(char *line, size_t capacity)
{
    static uint8_t printed_req[3];
    static uint8_t printed_rsp[3];
    static const char *const names[3] = {"DISC", "TYPE", "MEAS"};
    uint8_t i;
    int length = snprintf(
        line, capacity,
        "SENS9 disc_req=%lu disc_rsp=%lu type_req=%lu type_rsp=%lu "
        "meas_req=%lu meas_rsp=%lu last_req_ms=%lu last_rsp_ms=%lu "
        "txfail=%lu err=%lu\r\n",
        (unsigned long)g_sens9.req[0], (unsigned long)g_sens9.rsp[0],
        (unsigned long)g_sens9.req[1], (unsigned long)g_sens9.rsp[1],
        (unsigned long)g_sens9.req[2], (unsigned long)g_sens9.rsp[2],
        (unsigned long)g_sens9.last_req_ms,
        (unsigned long)g_sens9.last_rsp_ms,
        (unsigned long)g_sens9.txfail, (unsigned long)g_sens9.err);
    if ((length > 0) && ((size_t)length < capacity))
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                (uint16_t)length, 100U);
    for (i = 0U; i < 3U; i++)
    {
        if (!printed_req[i] && g_sens9.req[i] != 0U)
        {
            length = snprintf(line, capacity,
                "SENS9 %s req=%02X %02X %02X %02X\r\n", names[i],
                g_sens9.first_req[i][0], g_sens9.first_req[i][1],
                g_sens9.first_req[i][2], g_sens9.first_req[i][3]);
            if ((length > 0) && ((size_t)length < capacity))
            {
                (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                        (uint16_t)length, 100U);
                printed_req[i] = 1U;
            }
        }
        if (!printed_rsp[i] && g_sens9.rsp[i] != 0U)
        {
            length = (i == 0U) ?
                snprintf(line, capacity,
                    "SENS9 %s rsp=%02X %02X %02X %02X\r\n", names[i],
                    g_sens9.first_rsp[i][0], g_sens9.first_rsp[i][1],
                    g_sens9.first_rsp[i][2], g_sens9.first_rsp[i][3]) :
                snprintf(line, capacity,
                    "SENS9 %s rsp=%02X %02X %02X %02X %02X %02X\r\n",
                    names[i], g_sens9.first_rsp[i][0],
                    g_sens9.first_rsp[i][1], g_sens9.first_rsp[i][2],
                    g_sens9.first_rsp[i][3], g_sens9.first_rsp[i][4],
                    g_sens9.first_rsp[i][5]);
            if ((length > 0) && ((size_t)length < capacity))
            {
                (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                        (uint16_t)length, 100U);
                printed_rsp[i] = 1U;
            }
        }
    }
}
#endif

static void SensIbusUsart2_DiagTask(void *argument)
{
    char line[400];

    SensIbusTelemetrySourceDiag_t source;

    uint32_t previous_probe_ms = 0U;
    uint32_t previous_probe_sequence = 0U;
    (void)argument;
    for (;;)
    {
        int length;
        RemoteIbusProbe_t probe;
        uint32_t sampled_at_ms;
        uint32_t frames;
        uint32_t average_period_us;

        (void)osDelay(1000U);
        length = snprintf(
            line, sizeof(line),
            "SENS init=%lu create=%lu/%lu task=%lu rxarm=%lu rxcplt=%lu "
            "bytes=%lu frame=%lu post=%lu consume=%lu feed=%lu accept=%lu "
            "gen=%lu send=%lu txok=%lu txfail=%lu txdone=%lu err=%lu "
            "routes=d%lu/t%lu/m%lu absent=%lu unsupported=%lu snapshot=%lu "
            "gap=%lu silent=%lu last=%02X,%02X,%02X,%02X "
            "raw=%02X cmd=%02X addr=%u route=%s\r\n",
            (unsigned long)g_hil_diag.sens_init_called,
            (unsigned long)g_hil_diag.task_create_attempt,
            (unsigned long)g_hil_diag.task_create_ok,
            (unsigned long)g_hil_diag.task_entered,
            (unsigned long)g_hil_diag.rx_arm_count,
            (unsigned long)g_hil_diag.rx_cplt_count,
            (unsigned long)g_hil_diag.rx_byte_count,
            (unsigned long)g_hil_diag.frame_complete_count,
            (unsigned long)g_hil_diag.mailbox_post_count,
            (unsigned long)g_hil_diag.mailbox_consume_count,
            (unsigned long)g_hil_diag.responder_feed_count,
            (unsigned long)g_hil_diag.responder_accept_count,
            (unsigned long)g_hil_diag.response_generated_count,
            (unsigned long)g_hil_diag.send_called_count,
            (unsigned long)g_hil_diag.tx_start_ok_count,
            (unsigned long)g_hil_diag.tx_start_fail_count,
            (unsigned long)g_hil_diag.tx_complete_count,
            (unsigned long)g_hil_diag.uart_error_count,
            (unsigned long)g_hil_diag.route_discovery_count,
            (unsigned long)g_hil_diag.route_type_count,
            (unsigned long)g_hil_diag.route_measurement_count,
            (unsigned long)g_hil_diag.route_absent_address_count,
            (unsigned long)g_hil_diag.route_unsupported_command_count,
            (unsigned long)g_hil_diag.route_snapshot_unavailable_count,
            (unsigned long)(g_hil_diag.responder_accept_count -
                            g_hil_diag.response_generated_count),
            (unsigned long)(g_hil_diag.route_absent_address_count +
                            g_hil_diag.route_unsupported_command_count +
                            g_hil_diag.route_snapshot_unavailable_count),
            g_hil_diag.last_frame[0], g_hil_diag.last_frame[1],
            g_hil_diag.last_frame[2], g_hil_diag.last_frame[3],
            g_hil_diag.last_raw_cmd_addr,
            g_hil_diag.last_command_class,
            g_hil_diag.last_address,
            SensIbusUsart2_RouteName(g_hil_diag.last_route_result));
        if ((length > 0) && ((size_t)length < sizeof(line)))
        {
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                    (uint16_t)length, 100U);
        }
#if SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        SensIbusUsart2_PrintAddr9(line, sizeof(line));
#endif
        SensIbusUsart2_ReadTelemetrySourceDiag(&source);
        length = snprintf(
            line, sizeof(line),
            "TELSRC bms_v=%lu bms_i=%ld soc=%u "
            "wheel1=%ld wheel2=%ld wheel3=%ld wheel4=%ld "
            "ground_cmps=%u work_mode=%u run_permission=%u "
            "fault=0x%04X blade_rpm=%u\r\n",
            (unsigned long)source.bms_voltage_centivolts,
            (long)source.bms_current_deciamps,
            source.bms_soc_percent,
            (long)source.wheel_mm_per_s[0],
            (long)source.wheel_mm_per_s[1],
            (long)source.wheel_mm_per_s[2],
            (long)source.wheel_mm_per_s[3],
            source.ground_speed_cm_per_s,
            source.work_mode,
            source.run_permission,
            source.fault_bitmap,
            source.blade_rpm);
        if ((length > 0) && ((size_t)length < sizeof(line)))
        {
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                    (uint16_t)length, 100U);
        }
        sampled_at_ms = HAL_GetTick();
        if (RemoteBackend_ReadIbusProbe(&probe))
        {
            frames = (previous_probe_ms == 0U) ? 0U :
                     (uint32_t)(probe.frame_sequence - previous_probe_sequence);
            average_period_us = (frames == 0U) ? 0U :
                (uint32_t)(((uint64_t)(sampled_at_ms - previous_probe_ms) * 1000U) /
                           frames);
            length = snprintf(
                line, sizeof(line),
                "IBUS_PROBE ch11=%u ch12=%u ch13=%u ch14=%u "
                "seq=%lu frames=%lu avg_period_us=%lu fresh=%u "
                "timeout_seq=%lu last_ms=%lu age_ms=%lu sampled_at_ms=%lu\r\n",
                probe.channels[0], probe.channels[1],
                probe.channels[2], probe.channels[3],
                (unsigned long)probe.frame_sequence, (unsigned long)frames,
                (unsigned long)average_period_us,
                (unsigned int)probe.link_fresh,
                (unsigned long)probe.timeout_sequence,
                (unsigned long)probe.last_valid_ms,
                (unsigned long)(uint32_t)(sampled_at_ms - probe.last_valid_ms),
                (unsigned long)sampled_at_ms);
            if ((length > 0) && ((size_t)length < sizeof(line)))
            {
                (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                        (uint16_t)length, 100U);
            }
            previous_probe_ms = sampled_at_ms;
            previous_probe_sequence = probe.frame_sequence;
        }
#if EXP3_PWM_PROBE_ENABLED
        {
            Exp3BoardStatus status;
            Exp3PwmDiag pwm;
            exp3_board_status(&status);
            exp3_board_read_pwm_diag(&pwm);
            length = snprintf(
                line, sizeof(line),
                "EXP3 bound=%u wait=%u session=%08lX/%08lX matches=%lu "
                "id=%lu/%lu gen=%lu/%lu reqseq=%lu result=%u/%u ch4=%u blade=%u gate=%u "
                "ccr2=%lu confirm=%lu reject=%lu deny=%u raw=%04X frag=%u/%u "
                "log=%lu/%lu overflow=%lu\r\n",
                status.bound, status.waiting,
                (unsigned long)status.controller, (unsigned long)status.hmi,
                (unsigned long)status.matches,
                (unsigned long)pwm.request, (unsigned long)pwm.action,
                (unsigned long)pwm.generation, (unsigned long)pwm.ch4_epoch,
                (unsigned long)pwm.pending_sequence,
                pwm.result_status, pwm.last_event, pwm.ch4_raw,
                pwm.blade_state, pwm.gate_reason,
                (unsigned long)pwm.pwm_compare,
                (unsigned long)pwm.confirm_count,
                (unsigned long)pwm.reject_count,
                pwm.decision_reason,
                g_exp3_hil.link.last_word, g_exp3_hil.link.raw_next,
                (unsigned int)g_exp3_hil.link.raw_active,
                (unsigned long)g_exp3_hil.log_read,
                (unsigned long)g_exp3_hil.log_write,
                (unsigned long)g_exp3_hil.log_overflow);
            if ((length > 0) && ((size_t)length < sizeof(line)))
                (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                                        (uint16_t)length, 100U);
        }
#endif
        #if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
        SensIbusUsart2_PrintWheelDiag(line, sizeof(line));
        #endif
    }
}
#endif

static bool SensIbusUsart2_ReadSnapshot(void *context,
                                       TelemetrySnapshot_t *snapshot)
{
    TelemetrySnapshot_t current;

    (void)context;
    if (TelemetrySnapshot_Read(&current))
    {
        g_last_snapshot = current;
    }
    *snapshot = g_last_snapshot;
    return true;
}

static bool SensIbusUsart2_Send(void *context,
                                const uint8_t *response,
                                uint8_t length)
{
    (void)context;
    SENS_IBUS_DIAG_INC(send_called_count);
#if SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    uint8_t i;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
    uint8_t kind = ((g_responder.request[1] & 0x0FU) == 9U) ?
        SensIbusUsart2_Addr9Kind(g_responder.request[1] & 0xF0U) : 0U;
#endif

    if ((g_tx_busy != 0U) || (length > sizeof(g_tx_buffer)))
    {
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        if (kind != 0U) g_sens9.txfail++;
#endif
        return false;
    }

    for (i = 0U; i < length; i++)
    {
        g_tx_buffer[i] = response[i];
    }

    g_tx_busy = 1U;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
    g_sens9_tx_kind = kind;
#endif
    SENS_IBUS_SCOPE_MARKER_HIGH();
    (void)HAL_UART_AbortReceive(&huart2);
    if ((HAL_HalfDuplex_EnableTransmitter(&huart2) != HAL_OK) ||
        (HAL_UART_Transmit_IT(&huart2, g_tx_buffer, length) != HAL_OK))
    {
        SENS_IBUS_DIAG_INC(tx_start_fail_count);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        if (kind != 0U) g_sens9.txfail++;
        g_sens9_tx_kind = 0U;
#endif
        g_tx_busy = 0U;
        (void)HAL_HalfDuplex_EnableReceiver(&huart2);
        SENS_IBUS_SCOPE_MARKER_LOW();
        SENS_IBUS_DIAG_INC(rx_arm_count);
        (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
        return false;
    }
    SENS_IBUS_DIAG_INC(tx_start_ok_count);
    return true;
#else
    (void)response;
    (void)length;
    return false;
#endif
}

#if SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
static void SensIbusUsart2_Task(void *argument)
{
    uint8_t request[SENS_IBUS_REQUEST_SIZE];
    uint8_t i;

    (void)argument;
    SENS_IBUS_DIAG_INC(task_entered);
#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED && \
    !SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH
    SensIbusUsart2_RunScopeMarkerBootSelfTest();
#endif
    for (;;)
    {
        (void)osThreadFlagsWait(SENS_IBUS_TASK_FLAG_REQUEST,
                                osFlagsWaitAny, osWaitForever);
        for (i = 0U; i < SENS_IBUS_REQUEST_SIZE; i++)
        {
            request[i] = g_request_mailbox[i];
        }
        SENS_IBUS_DIAG_INC(mailbox_consume_count);
        g_request_ready = 0U;
        SENS_IBUS_DIAG_INC(responder_feed_count);
        SensIbusResponder_Feed(&g_responder, request, sizeof(request));
        if (g_tx_busy == 0U)
        {
            if (HAL_HalfDuplex_EnableReceiver(&huart2) == HAL_OK)
            {
                SENS_IBUS_DIAG_INC(rx_arm_count);
                (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
            }
        }
    }
}
#endif
#endif

void SensIbusUsart2_StartTask(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED && \
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    SENS_IBUS_DIAG_INC(task_create_attempt);
    g_task = osThreadNew(SensIbusUsart2_Task, NULL, &g_task_attributes);
    if (g_task != NULL)
    {
        SENS_IBUS_DIAG_INC(task_create_ok);
    }
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    (void)osThreadNew(SensIbusUsart2_DiagTask, NULL, &g_diag_task_attributes);
#endif
    if ((g_task != NULL) && (g_request_ready != 0U))
    {
        (void)osThreadFlagsSet(g_task, SENS_IBUS_TASK_FLAG_REQUEST);
    }
    if ((g_task != NULL) && (HAL_HalfDuplex_EnableReceiver(&huart2) == HAL_OK))
    {
        SENS_IBUS_DIAG_INC(rx_arm_count);
        (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
    }
#endif
}

void SensIbusUsart2_Init(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED
    SENS_IBUS_DIAG_INC(sens_init_called);
    uint8_t i;

    for (i = 0U; i < TELEMETRY_VALUE_COUNT; i++)
    {
        g_last_snapshot.values[i] = 0U;
    }
    g_last_snapshot.generated_at_ms = 0U;
    SensIbusResponder_Init(&g_responder, SensIbusUsart2_ReadSnapshot, NULL,
                           SensIbusUsart2_Send, NULL);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
    SensIbusResponder_SetObserver(&g_responder,
                                  SensIbusUsart2_ObserveResponder, NULL);
#endif
#if SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    g_request_ready = 0U;
    g_rx_count = 0U;
    g_tx_busy = 0U;
    SENS_IBUS_SCOPE_MARKER_LOW();
#endif
#endif
}

#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
void SensIbusUsart2_PublishTelemetrySourceDiag(
    const SensIbusTelemetrySourceDiag_t *source)
{
    uint8_t i;

    if (source == NULL)
    {
        return;
    }
    g_source_diag_sequence++;
    SENS_IBUS_COMPILER_BARRIER();
    g_source_diag.bms_voltage_centivolts = source->bms_voltage_centivolts;
    g_source_diag.bms_current_deciamps = source->bms_current_deciamps;
    g_source_diag.bms_soc_percent = source->bms_soc_percent;
    for (i = 0U; i < 4U; i++)
    {
        g_source_diag.wheel_mm_per_s[i] = source->wheel_mm_per_s[i];
    }
    g_source_diag.ground_speed_cm_per_s = source->ground_speed_cm_per_s;
    g_source_diag.blade_rpm = source->blade_rpm;
    g_source_diag.work_mode = source->work_mode;
    g_source_diag.run_permission = source->run_permission;
    g_source_diag.fault_bitmap = source->fault_bitmap;
    SENS_IBUS_COMPILER_BARRIER();
    g_source_diag_sequence++;
}
#endif

bool SensIbusUsart2_OutputEnabled(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED && \
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    return true;
#else
    return false;
#endif
}

void SensIbusUsart2_OnRxComplete(UART_HandleTypeDef *huart)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED && \
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        SENS_IBUS_DIAG_INC(rx_cplt_count);
        SENS_IBUS_DIAG_INC(rx_byte_count);
        if ((g_rx_count == 0U) && (g_rx_byte != SENS_IBUS_REQUEST_SIZE))
        {
            SENS_IBUS_DIAG_INC(rx_arm_count);
            (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
            return;
        }

        if ((g_request_ready != 0U) || (g_tx_busy != 0U))
        {
            g_rx_count = 0U;
            return;
        }

        g_request_mailbox[g_rx_count++] = g_rx_byte;
        if (g_rx_count == SENS_IBUS_REQUEST_SIZE)
        {
            SENS_IBUS_DIAG_INC(frame_complete_count);
            g_request_ready = 1U;
            SENS_IBUS_DIAG_INC(mailbox_post_count);
            g_rx_count = 0U;
            if (g_task != NULL)
            {
                (void)osThreadFlagsSet(g_task, SENS_IBUS_TASK_FLAG_REQUEST);
            }
            return;
        }
        SENS_IBUS_DIAG_INC(rx_arm_count);
        (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
    }
#else
    (void)huart;
#endif
}

void SensIbusUsart2_OnTxComplete(UART_HandleTypeDef *huart)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED && \
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        SENS_IBUS_DIAG_INC(tx_complete_count);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        SensIbusUsart2_Addr9TxComplete();
#endif
        g_tx_busy = 0U;
        g_rx_count = 0U;
        if (HAL_HalfDuplex_EnableReceiver(&huart2) == HAL_OK)
        {
            SENS_IBUS_SCOPE_MARKER_LOW();
            SENS_IBUS_DIAG_INC(rx_arm_count);
            (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
        }
        else
        {
            SENS_IBUS_SCOPE_MARKER_LOW();
        }
    }
#else
    (void)huart;
#endif
}

void SensIbusUsart2_OnError(UART_HandleTypeDef *huart)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !BLADE_HIL_UART_DEBUG_ENABLED && \
    SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        SENS_IBUS_DIAG_INC(uart_error_count);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && SENS_IBUS_HIL_ADDR9_PROBE_ENABLED
        if (g_sens9_tx_kind != 0U)
        {
            g_sens9.err++;
            g_sens9_tx_kind = 0U;
        }
#endif
        g_tx_busy = 0U;
        SENS_IBUS_SCOPE_MARKER_LOW();
        g_request_ready = 0U;
        g_rx_count = 0U;
        SensIbusResponder_ResetStream(&g_responder);
        (void)HAL_UART_Abort(&huart2);
        if (HAL_HalfDuplex_EnableReceiver(&huart2) == HAL_OK)
        {
            SENS_IBUS_DIAG_INC(rx_arm_count);
            (void)HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
        }
    }
#else
    (void)huart;
#endif
}
