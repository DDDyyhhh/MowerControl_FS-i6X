#ifndef SENS_IBUS_USART2_H
#define SENS_IBUS_USART2_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "blade_hil_uart_debug.h"

/* Ticket 08 frozen direct-connect electrical contract for PD5 / USART2_TX. */
#define SENS_IBUS_GPIO_MODE  GPIO_MODE_AF_OD
#define SENS_IBUS_GPIO_SPEED GPIO_SPEED_FREQ_LOW

#ifndef SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT
#define SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT 0
#endif

#if ((SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT != 0) && \
     (SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT != 1))
#error "SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT must be 0 or 1"
#endif

#if SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT && \
    !SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#error "SENS internal pull-up experiment requires SENS electrical output"
#endif

#if SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT
#define SENS_IBUS_GPIO_PULL GPIO_PULLUP
#else
#define SENS_IBUS_GPIO_PULL GPIO_NOPULL
#endif
#ifndef SENS_IBUS_HIL_SCOPE_MARKER_ENABLED
#define SENS_IBUS_HIL_SCOPE_MARKER_ENABLED 0
#endif

#if ((SENS_IBUS_HIL_SCOPE_MARKER_ENABLED != 0) && \
     (SENS_IBUS_HIL_SCOPE_MARKER_ENABLED != 1))
#error "SENS_IBUS_HIL_SCOPE_MARKER_ENABLED must be 0 or 1"
#endif

#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED && \
    !SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#error "SENS HIL scope marker requires SENS electrical output"
#endif

#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED && BLADE_HIL_UART_DEBUG_ENABLED
#error "SENS HIL scope marker cannot be used with USART2 debug"
#endif
#ifndef SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH
#define SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH 0
#endif

#if ((SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH != 0) && \
     (SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH != 1))
#error "SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH must be 0 or 1"
#endif

#if SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH && \
    !SENS_IBUS_HIL_SCOPE_MARKER_ENABLED
#error "SENS marker static-high sanity hold requires scope marker"
#endif
#ifndef SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
#define SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED 0
#endif

#if ((SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED != 0) && \
     (SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED != 1))
#error "SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED must be 0 or 1"
#endif

#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && \
    (!REMOTE_BACKEND_FLYSKY_IBUS || !SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED)
#error "SENS USART1 diagnostics require FlySky backend and SENS output"
#endif

typedef struct
{
    volatile uint32_t sens_init_called;
    volatile uint32_t task_create_attempt;
    volatile uint32_t task_create_ok;
    volatile uint32_t task_entered;
    volatile uint32_t rx_arm_count;
    volatile uint32_t rx_cplt_count;
    volatile uint32_t rx_byte_count;
    volatile uint32_t frame_complete_count;
    volatile uint32_t mailbox_post_count;
    volatile uint32_t mailbox_consume_count;
    volatile uint32_t responder_feed_count;
    volatile uint32_t responder_accept_count;
    volatile uint32_t response_generated_count;
    volatile uint32_t send_called_count;
    volatile uint32_t tx_start_ok_count;
    volatile uint32_t tx_start_fail_count;
    volatile uint32_t tx_complete_count;
    volatile uint32_t uart_error_count;
    volatile uint32_t route_discovery_count;
    volatile uint32_t route_type_count;
    volatile uint32_t route_measurement_count;
    volatile uint32_t route_absent_address_count;
    volatile uint32_t route_unsupported_command_count;
    volatile uint32_t route_snapshot_unavailable_count;
    volatile uint8_t last_frame[4U];
    volatile uint8_t last_raw_cmd_addr;
    volatile uint8_t last_command_class;
    volatile uint8_t last_address;
    volatile uint8_t last_route_result;
} SensIbusHilDiag_t;
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
typedef struct
{
    uint32_t bms_voltage_centivolts;
    int32_t bms_current_deciamps;
    uint16_t bms_soc_percent;
    int32_t wheel_mm_per_s[4];
    uint16_t ground_speed_cm_per_s;
    uint16_t blade_rpm;
    uint16_t work_mode;
    uint16_t run_permission;
    uint16_t fault_bitmap;
} SensIbusTelemetrySourceDiag_t;
#endif

/* Ticket 11 HIL-only TX ownership marker. Production default is disabled. */
#define SENS_IBUS_HIL_SCOPE_MARKER_PORT  GPIOD
#define SENS_IBUS_HIL_SCOPE_MARKER_PIN   GPIO_PIN_6
#define SENS_IBUS_HIL_SCOPE_MARKER_MODE  GPIO_MODE_OUTPUT_PP
#define SENS_IBUS_HIL_SCOPE_MARKER_PULL  GPIO_NOPULL
#define SENS_IBUS_HIL_SCOPE_MARKER_SPEED GPIO_SPEED_FREQ_LOW

#if BLADE_HIL_UART_DEBUG_ENABLED && SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED
#error "USART2 SENS and blade HIL debug cannot be enabled together"
#endif

void SensIbusUsart2_StartTask(void);
void SensIbusUsart2_Init(void);
bool SensIbusUsart2_OutputEnabled(void);
void SensIbusUsart2_OnRxComplete(UART_HandleTypeDef *huart);
void SensIbusUsart2_OnTxComplete(UART_HandleTypeDef *huart);
void SensIbusUsart2_OnError(UART_HandleTypeDef *huart);
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED
void SensIbusUsart2_PublishTelemetrySourceDiag(
    const SensIbusTelemetrySourceDiag_t *source);
#endif

#endif
