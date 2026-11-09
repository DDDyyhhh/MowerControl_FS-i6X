#include "remote_backend.h"

#include <limits.h>
#include <string.h>

#if REMOTE_BACKEND_FLYSKY_IBUS
#include "blade_confirm_ch6_adapter.h"
#include "../../../experiments/ticket03/exp3.h"
#include "flysky_ibus_servo.h"
#endif

#if !defined(REMOTE_BACKEND_HOST)
#include "A28.h"
#include "cmsis_os.h"
#include "main.h"
#include "app_context.h"
#include "chassis_control.h"
#include "tricolor_led.h"

#include "pairing_receiver.h"
#include "usart.h"
extern uint8_t rx_dma_buffer[RX_BUF_SIZE];
extern osMessageQueueId_t Queue_RxDataHandle;
/* C99 compile-time check: DMA payload and queued payload must stay identical. */
typedef char RemoteRxCapacityMatchesDma[(REMOTE_RX_BUFFER_SIZE == RX_BUF_SIZE) ? 1 : -1];
#endif

#if !defined(REMOTE_BACKEND_HOST)
#define REMOTE_UART_USART1 ((uintptr_t)USART1)
#define REMOTE_UART_USART3 ((uintptr_t)USART3)
#else
#define REMOTE_UART_USART1 ((uintptr_t)1U)
#define REMOTE_UART_USART3 ((uintptr_t)3U)
#endif


#define LEGACY_FRAME_STICK_SIZE       10U
#define LEGACY_FRAME_BUTTON_SIZE      15U
#define LEGACY_FRAME_PAIRING_MAX_SIZE 10U
#define REMOTE_AXIS_CENTER            2048U

static RemoteBackendClock_t g_clock;
static RemoteCommand_t g_command;
static uint32_t g_now_ms;
/* ISR owns ingress sequence, overflow and UART error metadata; Comm owns the other fields.
 * Each counter is independent, aligned 32-bit, and saturating. */
static volatile uint32_t g_rx_sequence;
static volatile RemoteRxDiagnostics_t g_rx_diagnostics;
static uint32_t g_rx_last_sequence;
static bool g_rx_has_sequence;

static void RemoteBackend_Count(volatile uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}
#if REMOTE_BACKEND_FLYSKY_IBUS
static FlySkyIbusServo_t g_flysky_ibus;
/* Comm owns the parser. Only this fixed-size publication crosses tasks. */
static struct
{
    FlySkyIbusSnapshot_t snapshot;
    bool available;
} g_flysky_publication;
/* Motion owns this observation; it must survive a delayed Comm publication. */
static bool g_motion_timeout_latched;
static RemoteAuthorization_t g_remote_authorization;
static RemoteAuthorizationInput_t g_authorization_input;
static BladeConfirmCh6Adapter_t g_blade_confirm_adapter;
static RemoteSafetyInput_t g_remote_safety;
static uint32_t g_active_blade_request_sequence;
static GrassBagRemote_t g_grass_bag_remote;
#endif

static uint32_t RemoteBackend_Now(void)
{
    if (g_clock.now_ms != NULL)
    {
        return g_clock.now_ms(g_clock.context);
    }

#if !defined(REMOTE_BACKEND_HOST)
    return HAL_GetTick();
#else
    return g_now_ms;
#endif
}

float RemoteBackend_MapTravelSpeed(uint16_t pulse_us)
{
    if (pulse_us <= REMOTE_TRAVEL_SPEED_SLOW_US)
    {
        return REMOTE_TRAVEL_SPEED_MIN_MPS;
    }
    if (pulse_us >= REMOTE_TRAVEL_SPEED_FAST_US)
    {
        return REMOTE_TRAVEL_SPEED_MAX_MPS;
    }
    return REMOTE_TRAVEL_SPEED_MIN_MPS +
           ((float)(pulse_us - REMOTE_TRAVEL_SPEED_SLOW_US) /
            (float)(REMOTE_TRAVEL_SPEED_FAST_US - REMOTE_TRAVEL_SPEED_SLOW_US)) *
               (REMOTE_TRAVEL_SPEED_MAX_MPS - REMOTE_TRAVEL_SPEED_MIN_MPS);
}

float RemoteBackend_ApplyReverseSpeed(float speed_mps, bool reverse)
{
    return reverse ? speed_mps * REMOTE_TRAVEL_REVERSE_FACTOR : speed_mps;
}

#if REMOTE_BACKEND_A28_E34
static void RemoteBackend_PublishLegacy(uint32_t now_ms)
{
    g_command.last_valid_ms = now_ms;
    g_command.frame_sequence++;
    g_command.connected = true;
    g_command.link_fresh = true;
    g_command.stable_online = true;
    g_command.valid = true;
    g_command.run_permitted = true;
}
#endif

static uint32_t RemoteBackend_EnterSnapshotCritical(void)
{
#if !defined(REMOTE_BACKEND_HOST) || defined(REMOTE_BACKEND_IRQ_TEST)
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __COMPILER_BARRIER();
    return mask;
#else
    return 0U;
#endif
}

static void RemoteBackend_ExitSnapshotCritical(uint32_t mask)
{
#if !defined(REMOTE_BACKEND_HOST) || defined(REMOTE_BACKEND_IRQ_TEST)
    __COMPILER_BARRIER();
    __set_PRIMASK(mask);
#else
    (void)mask;
#endif
}

void RemoteBackend_GetRxDiagnostics(RemoteRxDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL) return;
    uint32_t mask = RemoteBackend_EnterSnapshotCritical();
    *diagnostics = g_rx_diagnostics;
    RemoteBackend_ExitSnapshotCritical(mask);
}

#if REMOTE_BACKEND_FLYSKY_IBUS
static bool RemoteBackend_ReadPublishedFlySky(FlySkyIbusSnapshot_t *snapshot)
{
    uint32_t mask = RemoteBackend_EnterSnapshotCritical();
    bool available = g_flysky_publication.available;
    if (available)
    {
        *snapshot = g_flysky_publication.snapshot;
    }
    RemoteBackend_ExitSnapshotCritical(mask);
    return available;
}

static void RemoteBackend_ResetRxStream(void)
{
    if (g_flysky_ibus.stream_count != 0U)
        RemoteBackend_Count(&g_rx_diagnostics.stream_resets);
    FlySkyIbusServo_ResetStream(&g_flysky_ibus);
}

/* Parse in reception time, then expire in wall time before publishing. */
static void RemoteBackend_ServiceFlySky(const RemoteRxMsg_t *message)
{
    FlySkyIbusSnapshot_t snapshot;
    uint32_t now_ms = RemoteBackend_Now();
    if (FlySkyIbusServo_ExpireStream(&g_flysky_ibus, now_ms))
        RemoteBackend_Count(&g_rx_diagnostics.stream_resets);
    if (message != NULL)
    {
        FlySkyIbusServo_Advance(&g_flysky_ibus, message->received_at_ms);
        FlySkyIbusServo_FeedReceived(&g_flysky_ibus, message->data, message->len,
                                     message->received_at_ms, now_ms);
    }
    FlySkyIbusServo_Advance(&g_flysky_ibus, RemoteBackend_Now());
    g_rx_diagnostics.crc_errors = g_flysky_ibus.crc_errors;
    g_rx_diagnostics.invalid_channel_frames = g_flysky_ibus.invalid_channel_frames;
    if (FlySkyIbusServo_ReadSnapshot(&g_flysky_ibus, &snapshot))
    {
        uint32_t mask = RemoteBackend_EnterSnapshotCritical();
        g_flysky_publication.snapshot = snapshot;
        g_flysky_publication.available = true;
        RemoteBackend_ExitSnapshotCritical(mask);
    }
}

/* Pure reader-side watchdog: never mutates Comm state or its event counters.
 * The configuration is immutable after initialization. */
static void RemoteBackend_ExpireFlySkySnapshot(FlySkyIbusSnapshot_t *snapshot,
                                             uint32_t now_ms)
{
    if ((uint32_t)(now_ms - snapshot->last_valid_ms) >= g_flysky_ibus.config.timeout_ms)
    {
        snapshot->link_fresh = false;
        snapshot->stable_online = false;
        snapshot->timeout_latched = true;
        snapshot->steering = REMOTE_AXIS_CENTER;
        snapshot->throttle = REMOTE_AXIS_CENTER;
        snapshot->travel_speed_raw = 0U;
    }
}

static void RemoteBackend_UpdateAuthorization(const FlySkyIbusSnapshot_t *snapshot,
                                              RemoteSafetyInput_t safety,
                                              uint32_t now_ms)
{
    RemoteAuthorizationInput_t input;

    input.link_fresh = snapshot->link_fresh;
    input.link_timed_out = !snapshot->link_fresh;
    input.stable_online = snapshot->stable_online;
    input.physical_latched = safety.physical_latched;
    input.emergency_active = safety.emergency_active;
    input.collision_active = safety.collision_active;
    input.tilt_active = safety.tilt_active;
    input.steering_raw = snapshot->channels[g_flysky_ibus.config.steering.channel_index];
    input.throttle_raw = snapshot->channels[g_flysky_ibus.config.throttle.channel_index];
    input.run_enable_raw = snapshot->run_enable_raw;
    input.blade_gear_raw = snapshot->blade_gear_raw;
    input.unlock_raw = snapshot->unlock_raw;
    input.ch4_raw = snapshot->blade_start_raw;
    input.frame_sequence = snapshot->frame_sequence;
    input.timeout_sequence = snapshot->timeout_sequence;
    input.ch5_stop_sequence = snapshot->ch5_stop_sequence;
    g_authorization_input = input;
    RemoteAuthorization_Advance(&g_remote_authorization, &input, now_ms);
    BladeConfirmCh6Input_t ch6_in;
    ch6_in.ch6_raw = snapshot->blade_confirm_raw;
    ch6_in.link_fresh = snapshot->link_fresh;
    ch6_in.timeout_latched = snapshot->timeout_latched;
    ch6_in.frame_sequence = snapshot->frame_sequence;
    ch6_in.active_request_sequence = g_active_blade_request_sequence;
#if !MOWER_BLADE_OK_ENABLED
    BladeConfirmCh6Adapter_Advance(&g_blade_confirm_adapter, &ch6_in);
#else
    (void)ch6_in;
#endif
}

#if REMOTE_BACKEND_FLYSKY_IBUS
static uint8_t RemoteBackend_DecodeBladeHeight(const FlySkyIbusConfig_t *config,
                                               uint16_t pulse_us,
                                               uint8_t current_level)
{
    uint16_t t1 = (config != NULL && config->blade_height_t1_us != 0U) ?
                  config->blade_height_t1_us : REMOTE_BLADE_HEIGHT_T1_US;
    uint16_t t2 = (config != NULL && config->blade_height_t2_us != 0U) ?
                  config->blade_height_t2_us : REMOTE_BLADE_HEIGHT_T2_US;
    uint16_t t3 = (config != NULL && config->blade_height_t3_us != 0U) ?
                  config->blade_height_t3_us : REMOTE_BLADE_HEIGHT_T3_US;
    uint16_t t4 = (config != NULL && config->blade_height_t4_us != 0U) ?
                  config->blade_height_t4_us : REMOTE_BLADE_HEIGHT_T4_US;
    uint16_t hyst = (config != NULL && config->blade_height_hysteresis_us != 0U) ?
                    config->blade_height_hysteresis_us : REMOTE_BLADE_HEIGHT_HYSTERESIS_US;

    if ((current_level < 1U) || (current_level > 5U))
    {
        if (pulse_us < t1)
        {
            return 1U;
        }
        if (pulse_us < t2)
        {
            return 2U;
        }
        if (pulse_us < t3)
        {
            return 3U;
        }
        if (pulse_us < t4)
        {
            return 4U;
        }
        return 5U;
    }

    /* Downward transitions with hysteresis */
    if (pulse_us < (t1 - hyst))
    {
        return 1U;
    }
    if ((current_level > 2U) && (pulse_us < (t2 - hyst)))
    {
        return 2U;
    }
    if ((current_level > 3U) && (pulse_us < (t3 - hyst)))
    {
        return 3U;
    }
    if ((current_level > 4U) && (pulse_us < (t4 - hyst)))
    {
        return 4U;
    }

    /* Upward transitions with hysteresis */
    if (pulse_us > (t4 + hyst))
    {
        return 5U;
    }
    if ((current_level < 4U) && (pulse_us > (t3 + hyst)))
    {
        return 4U;
    }
    if ((current_level < 3U) && (pulse_us > (t2 + hyst)))
    {
        return 3U;
    }
    if ((current_level < 2U) && (pulse_us > (t1 + hyst)))
    {
        return 2U;
    }

    return current_level;
}
#endif

static void RemoteBackend_CopyFlySkySnapshot(const FlySkyIbusSnapshot_t *snapshot)
{
    RemoteAuthorizationOutput_t authorization;
    uint32_t previous_invalidation = g_command.command_invalidation_sequence;
    uint8_t previous_height_level = g_command.height_level;

    RemoteAuthorization_GetOutput(&g_remote_authorization, &authorization);
    memset(&g_command, 0, sizeof(g_command));
    g_command.steering = authorization.run_permitted ? snapshot->steering : REMOTE_AXIS_CENTER;
    g_command.throttle = authorization.run_permitted ? snapshot->throttle : REMOTE_AXIS_CENTER;
    g_command.blade_height = snapshot->blade_height_raw;
    g_command.blade_gear_raw = snapshot->blade_gear_raw;
    g_command.travel_speed_raw = authorization.run_permitted ? snapshot->travel_speed_raw : 0U;
    g_command.travel_speed_mps = authorization.run_permitted && snapshot->travel_speed_configured ?
                                     RemoteBackend_MapTravelSpeed(snapshot->travel_speed_raw) : 0.0f;
    g_command.run_enable_raw = snapshot->run_enable_raw;
    g_command.unlock_raw = snapshot->unlock_raw;
    g_command.blade_start_raw = snapshot->blade_start_raw;
    g_command.blade_confirm_raw = snapshot->blade_confirm_raw;
    g_command.grass_bag_raw = snapshot->grass_bag_raw;
    g_command.grass_bag_configured = snapshot->grass_bag_configured;
    g_command.grass_bag_target = g_grass_bag_remote.stable_target;
    g_command.blade_start_configured = snapshot->blade_start_configured;
    g_command.blade_confirm_configured = snapshot->blade_confirm_configured;
    g_command.has_confirm_event = BladeConfirmCh6Adapter_HasEvent(&g_blade_confirm_adapter);
    g_command.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&g_blade_confirm_adapter);
    g_command.confirm_request_sequence =
        g_blade_confirm_adapter.latest_event.request_sequence;
#if MOWER_BLADE_OK_ENABLED
    g_command.has_confirm_event = false;
    g_command.confirm_event_sequence = 0U;
    g_command.confirm_request_sequence = 0U;
#endif
    memcpy(g_command.channels, snapshot->channels, sizeof(g_command.channels));
    g_command.remote_run_enable = MOWER_BLADE_OK_ENABLED ?
        authorization.run_enable != REMOTE_SWITCH_UNKNOWN :
        authorization.run_enable == REMOTE_SWITCH_ON;
    g_command.remote_unlock = (uint8_t)authorization.unlock;
    g_command.receiver_failsafe = false;
    g_command.remote_recoverable_latched = authorization.remote_recoverable_latched;
    g_command.run_permitted = authorization.run_permitted;
    g_command.blade_gear = (uint8_t)authorization.blade_gear;
    if (snapshot->blade_height_configured)
    {
        g_command.height_level = RemoteBackend_DecodeBladeHeight(
            &g_flysky_ibus.config, snapshot->blade_height_raw, previous_height_level);
    }
    else
    {
        g_command.height_level = 0U;
    }
    g_command.connected = snapshot->stable_online && snapshot->link_fresh;
    g_command.valid = snapshot->link_fresh;
    g_command.link_fresh = snapshot->link_fresh;
    g_command.stable_online = snapshot->stable_online;
    g_command.timeout_latched = snapshot->timeout_latched;
    g_command.run_enable_configured = snapshot->run_enable_configured;
    g_command.blade_height_configured = snapshot->blade_height_configured;
    g_command.travel_speed_configured = snapshot->travel_speed_configured;
    g_command.frame_sequence = snapshot->frame_sequence;
    g_command.timeout_sequence = snapshot->timeout_sequence;
    g_command.ch5_stop_sequence = snapshot->ch5_stop_sequence;
    g_command.command_invalidation_sequence = authorization.command_invalidation_sequence;
    g_command.authorization_state = authorization.state;
    g_command.recoverable_latch_reason = authorization.latch_reason;
    g_command.last_valid_ms = snapshot->last_valid_ms;

    if (authorization.command_invalidation_sequence != previous_invalidation)
    {
        g_command.steering = REMOTE_AXIS_CENTER;
        g_command.throttle = REMOTE_AXIS_CENTER;
        g_command.travel_speed_raw = 0U;
        g_command.travel_speed_mps = 0.0f;
    }
}

static void RemoteBackend_RefreshFlySkyCommand(uint32_t now_ms)
{
    FlySkyIbusSnapshot_t snapshot;
    if (RemoteBackend_ReadPublishedFlySky(&snapshot))
    {
        RemoteBackend_ExpireFlySkySnapshot(&snapshot, now_ms);
        g_motion_timeout_latched |= snapshot.timeout_latched;
        snapshot.timeout_latched = g_motion_timeout_latched;
        RemoteBackend_UpdateAuthorization(&snapshot, g_remote_safety, now_ms);
        RemoteBackend_CopyFlySkySnapshot(&snapshot);
    }
}
#endif

uint8_t RemoteBackend_UnlockBlockReason(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    return RemoteAuthorization_BlockReason(&g_remote_authorization,
        &g_authorization_input, RemoteBackend_Now());
#else
    return 1U;
#endif
}

bool RemoteBackend_ConfirmUnlock(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    /* Consume any CH5 event received since the last Motion snapshot before OK. */
    RemoteBackend_RefreshFlySkyCommand(RemoteBackend_Now());
    return RemoteAuthorization_ConfirmOK(&g_remote_authorization,
                                         &g_authorization_input, RemoteBackend_Now());
#else
    return false;
#endif
}

void RemoteBackend_CancelUnlock(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    RemoteAuthorization_CancelOK(&g_remote_authorization);
#endif
}

void RemoteBackend_RevokeAuthorization(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    RemoteAuthorization_Revoke(&g_remote_authorization);
#endif
}

void RemoteBackend_GetJoystickOutput(uint16_t joystick[4])
{
    if (joystick == NULL)
    {
        return;
    }
#if REMOTE_BACKEND_FLYSKY_IBUS
    joystick[0] = g_command.steering;
    joystick[1] = g_command.throttle;
    joystick[2] = g_command.steering;
    joystick[3] = g_command.throttle;
#else
    joystick[0] = g_command.channels[0];
    joystick[1] = g_command.channels[1];
    joystick[2] = g_command.channels[2];
    joystick[3] = g_command.channels[3];
#endif
}

#if !defined(REMOTE_BACKEND_HOST)
static void RemoteBackend_ApplyCommand(void)
{
    RemoteBackend_GetJoystickOutput(g_app.Joystick);
#if REMOTE_BACKEND_FLYSKY_IBUS
    /* FlySky CH7 selects blade gear, not the legacy travel gear. */
    if (g_command.run_permitted)
    {
        if (g_app.lock != 0U) {
            g_app.collision_reason_latched = 0U;
            g_app.emergency_reason_latched = 0U;
        }
        g_app.lock = 0U;
        g_app.physical_safety_latched = 0U;
        /* Advance has consumed a safe OK and a fresh frame. Readers in this
         * same Motion tick must observe the cleared latch, not revoke it again. */
        g_remote_safety.physical_latched = false;
        g_authorization_input.physical_latched = false;
        g_app.g_collision_latched = 0U;
        if (g_app.g_system_report_state != REPORT_NONE)
        {
            g_app.g_system_report_state = REPORT_NONE;
            RGB_LED_SetColor(LED_1, RGB_COLOR_GREEN);
            RGB_LED_SetColor(LED_2, RGB_COLOR_GREEN);
        }
    }
    else
    {
        g_app.lock = 1U;
        g_app.g_collision_latched = 1U;
        g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = 0U;
    }
    g_app.receiver_status[STATUS_INDEX_LOCK] = g_command.run_permitted ? 1U : 0U;
    if ((Ctrl_Mode == 0U) && (g_command.height_level >= 1U) && (g_command.height_level <= 5U))
    {
        g_app.knob_height = g_command.height_level;
    }
#else
    uint8_t config_flags;

    memcpy(g_app.key, g_command.keys, sizeof(g_app.key));
    g_app.knob_gear = g_command.gear;

    if (Ctrl_Mode == 0U)
    {
        g_app.knob_blade = g_command.blade_speed;
        g_app.knob_height = g_command.height_level;
    }

    config_flags = g_command.config_flags;
    g_app.cfg_ultrasonic_en = (config_flags & (1U << 0)) ? 1U : 0U;
    g_app.cfg_collision_en = (config_flags & (1U << 1)) ? 1U : 0U;
    g_app.cfg_reverse_limit_en = (config_flags & (1U << 2)) ? 1U : 0U;
    g_app.cfg_tilt_sensor_en = (config_flags & (1U << 3)) ? 1U : 0U;
    g_app.cfg_lawn_protect_en = (config_flags & (1U << 6)) ? 1U : 0U;
    if ((config_flags & (1U << 4)) != 0U)
    {
        g_app.trigger_blade_init = 1U;
    }
    if ((config_flags & (1U << 5)) != 0U)
    {
        g_app.trigger_cruise_req = 1U;
    }
    g_app.cfg_op_mode = g_command.operation_mode;
#endif

    g_app.last_receive_timestamp = g_command.last_valid_ms;
    g_app.communication_connected = g_command.connected;
}
#endif

#if REMOTE_BACKEND_A28_E34
static uint16_t RemoteBackend_ReadBe12(const uint8_t *data)
{
    return (uint16_t)((((uint16_t)data[0] << 8) | data[1]) & 0x0FFFU);
}

static void RemoteBackend_HandleLegacy(const uint8_t *data, uint16_t length, uint32_t received_at_ms)
{
    uint16_t i;

    for (i = 0U; i < length; i++)
    {
        if ((data[i] == 0xABU) &&
            ((uint32_t)i + LEGACY_FRAME_STICK_SIZE <= length) &&
            (data[i + 9U] == 0xBAU))
        {
            g_command.channels[0] = RemoteBackend_ReadBe12(&data[i + 1U]);
            g_command.channels[1] = RemoteBackend_ReadBe12(&data[i + 3U]);
            g_command.channels[2] = RemoteBackend_ReadBe12(&data[i + 5U]);
            g_command.channels[3] = RemoteBackend_ReadBe12(&data[i + 7U]);
            RemoteBackend_PublishLegacy(received_at_ms);
            i += 9U;
        }
        else if ((data[i] == 0xCCU) &&
                 ((uint32_t)i + LEGACY_FRAME_BUTTON_SIZE <= length) &&
                 (data[i + 14U] == 0xDDU))
        {
            memcpy(g_command.keys, &data[i + 1U], sizeof(g_command.keys));
            g_command.gear = data[i + 9U];
            g_command.blade_speed = data[i + 10U];
            g_command.height_level = data[i + 11U];
            g_command.config_flags = data[i + 12U];
            g_command.operation_mode = data[i + 13U];
            g_command.valid = true;
            i += 14U;
        }
#if !defined(REMOTE_BACKEND_HOST)
        else if (data[i] == 0xAAU)
        {
            uint16_t j;
            for (j = 1U;
                 (j < LEGACY_FRAME_PAIRING_MAX_SIZE) &&
                 ((uint32_t)i + j < length);
                 j++)
            {
                if (data[i + j] == 0xBBU)
                {
                    pairing_receiver_handle_message((uint8_t *)&data[i],
                                                    (uint16_t)(j + 1U));
                    i += j;
                    break;
                }
            }
        }
#endif
    }
}
#endif

FlySkyIbusConfig_t RemoteBackend_DefaultFlySkyConfig(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    return FlySkyIbusServo_DefaultConfig();
#else
    FlySkyIbusConfig_t config;
    memset(&config, 0, sizeof(config));
    return config;
#endif
}

void RemoteBackend_InitWithFlySkyConfig(RemoteBackendClock_t clock,
                                        const FlySkyIbusConfig_t *config)
{
    memset(&g_command, 0, sizeof(g_command));
    g_clock = clock;
    g_now_ms = 0U;
    g_rx_sequence = g_rx_last_sequence = 0U;
    g_rx_has_sequence = false;
    memset((void *)&g_rx_diagnostics, 0, sizeof(g_rx_diagnostics));
#if REMOTE_BACKEND_FLYSKY_IBUS
    g_active_blade_request_sequence = 0U;
    memset(&g_grass_bag_remote, 0, sizeof(g_grass_bag_remote));
    memset(&g_remote_safety, 0, sizeof(g_remote_safety));
    memset(&g_authorization_input, 0, sizeof(g_authorization_input));
    memset(&g_flysky_publication, 0, sizeof(g_flysky_publication));
    g_motion_timeout_latched = false;
    FlySkyIbusServo_Init(&g_flysky_ibus, config);
    RemoteAuthorization_Init(&g_remote_authorization, config);
    BladeConfirmCh6Config_t ch6_cfg;
    if (config != NULL)
    {
        ch6_cfg.low_us = config->blade_confirm_low_us;
        ch6_cfg.high_us = config->blade_confirm_high_us;
        ch6_cfg.tolerance_us = config->blade_confirm_tolerance_us;
        ch6_cfg.confirm_frames = config->blade_confirm_frames;
        ch6_cfg.semantics_configured = config->blade_confirm_semantics_configured;
    }
    else
    {
        ch6_cfg = BladeConfirmCh6Adapter_DefaultConfig();
    }
    BladeConfirmCh6Adapter_Init(&g_blade_confirm_adapter, &ch6_cfg);
    g_command.steering = REMOTE_AXIS_CENTER;
    g_command.throttle = REMOTE_AXIS_CENTER;
    g_command.remote_recoverable_latched = true;
#else
    (void)config;
    g_command.channels[0] = REMOTE_AXIS_CENTER;
    g_command.channels[1] = REMOTE_AXIS_CENTER;
    g_command.channels[2] = REMOTE_AXIS_CENTER;
    g_command.channels[3] = REMOTE_AXIS_CENTER;
#endif
}

void RemoteBackend_Init(RemoteBackendClock_t clock)
{
    FlySkyIbusConfig_t config = RemoteBackend_DefaultFlySkyConfig();
    RemoteBackend_InitWithFlySkyConfig(clock, &config);
}

void RemoteBackend_Start(void)
{
#if !defined(REMOTE_BACKEND_HOST)
#if REMOTE_BACKEND_A28_E34
    A28_Set_FullDuplex_Mode();
    RF_Module_Set_Trans_Mode();
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buffer, RX_BUF_SIZE);
#elif REMOTE_BACKEND_FLYSKY_IBUS
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma_buffer, RX_BUF_SIZE);
#endif
#endif
}

static void RemoteBackend_ServiceNoData(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    RemoteBackend_ServiceFlySky(NULL);
#else
    if (g_command.connected && (uint32_t)(RemoteBackend_Now() - g_command.last_valid_ms) >=
        REMOTE_LEGACY_TIMEOUT_MS)
    {
        g_command.connected = g_command.link_fresh = g_command.stable_online = false;
        g_command.valid = g_command.run_permitted = false;
    }
#endif
}

void RemoteBackend_ProcessRxMessage(const RemoteRxMsg_t *message)
{
    uint32_t now_ms = RemoteBackend_Now();
    if (message == NULL || message->len == 0U || message->len > REMOTE_RX_BUFFER_SIZE)
    {
        RemoteBackend_Count(&g_rx_diagnostics.invalid_messages);
#if REMOTE_BACKEND_FLYSKY_IBUS
        RemoteBackend_ResetRxStream();
#endif
        RemoteBackend_ServiceNoData();
        return;
    }
    uint32_t age = now_ms - message->received_at_ms;
    if (age > g_rx_diagnostics.max_queue_age_ms) g_rx_diagnostics.max_queue_age_ms = age;
    uint32_t delta = message->sequence - g_rx_last_sequence;
    if (g_rx_has_sequence && (delta == 0U || delta > UINT32_MAX / 2U))
    {
        RemoteBackend_Count(&g_rx_diagnostics.invalid_messages);
#if REMOTE_BACKEND_FLYSKY_IBUS
        RemoteBackend_ResetRxStream();
#endif
        RemoteBackend_ServiceNoData();
        return;
    }
#if REMOTE_BACKEND_FLYSKY_IBUS
    if (g_rx_has_sequence && delta != 1U) RemoteBackend_ResetRxStream();
#endif
    g_rx_has_sequence = true;
    g_rx_last_sequence = message->sequence;
#if REMOTE_BACKEND_FLYSKY_IBUS
    uint32_t timeout_ms = g_flysky_ibus.config.timeout_ms;
#else
    uint32_t timeout_ms = REMOTE_LEGACY_TIMEOUT_MS;
#endif
    if (age >= timeout_ms)
    {
        RemoteBackend_Count(&g_rx_diagnostics.expired_messages);
#if REMOTE_BACKEND_FLYSKY_IBUS
        RemoteBackend_ResetRxStream();
#endif
        RemoteBackend_ServiceNoData();
        return;
    }
#if REMOTE_BACKEND_FLYSKY_IBUS
    RemoteBackend_ServiceFlySky(message);
#else
    RemoteBackend_HandleLegacy(message->data, message->len, message->received_at_ms);
    RemoteBackend_ServiceNoData();
#if !defined(REMOTE_BACKEND_HOST)
    RemoteBackend_ApplyCommand();
#endif
#endif
}

void RemoteBackend_Process(void)
{
#if !defined(REMOTE_BACKEND_HOST)
    RemoteRxMsg_t message;
#if REMOTE_BACKEND_A28_E34
    const uint32_t wait_ms = 2000U;
#else
    const uint32_t wait_ms = 10U;
#endif
    if (osMessageQueueGet(Queue_RxDataHandle, &message, NULL, wait_ms) == osOK)
        RemoteBackend_ProcessRxMessage(&message);
    else
    {
        RemoteBackend_ServiceNoData();
#if REMOTE_BACKEND_A28_E34
        HAL_UART_AbortReceive(&huart3);
        HAL_UART_DMAStop(&huart3);
        __HAL_UART_CLEAR_OREFLAG(&huart3);
        __HAL_UART_CLEAR_NEFLAG(&huart3);
        __HAL_UART_CLEAR_FEFLAG(&huart3);
        (void)huart3.Instance->DR;
        HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buffer, RX_BUF_SIZE);
#endif
    }
#else
    RemoteBackend_ServiceNoData();
#endif
}

void RemoteBackend_OnUartRx(uintptr_t uart_instance, const uint8_t *data, uint16_t length)
{
    /* Callback entry time, before copying or any scheduling delay. */
    uint32_t received_at_ms = RemoteBackend_Now();
    if (data == NULL || length == 0U || !RemoteBackend_UsesUart(uart_instance)) return;
#if !defined(REMOTE_BACKEND_HOST)
    RemoteRxMsg_t message;
    message.received_at_ms = received_at_ms;
    message.sequence = ++g_rx_sequence;
    message.len = length > REMOTE_RX_BUFFER_SIZE ? REMOTE_RX_BUFFER_SIZE : length;
    memcpy(message.data, data, message.len);
    if (Queue_RxDataHandle == NULL ||
        osMessageQueuePut(Queue_RxDataHandle, &message, 0U, 0U) != osOK)
        RemoteBackend_Count(&g_rx_diagnostics.queue_overflows);
#else
    /* Existing byte-stream host callers may supply more than one DMA block. */
    while (length != 0U)
    {
        RemoteRxMsg_t message;
        message.received_at_ms = received_at_ms;
        message.sequence = ++g_rx_sequence;
        message.len = length > REMOTE_RX_BUFFER_SIZE ? REMOTE_RX_BUFFER_SIZE : length;
        memcpy(message.data, data, message.len);
        RemoteBackend_ProcessRxMessage(&message);
        data += message.len;
        length = (uint16_t)(length - message.len);
    }
#endif
}

void RemoteBackend_OnUartError(uintptr_t uart_instance)
{
    if (!RemoteBackend_UsesUart(uart_instance)) return;
    RemoteBackend_Count(&g_rx_diagnostics.uart_errors);
    /* ISR must not touch Comm's stream. Next dequeued block sees the gap. */
    ++g_rx_sequence;
#if !defined(REMOTE_BACKEND_HOST)
#if REMOTE_BACKEND_A28_E34
    g_rx_diagnostics.last_uart_error = huart3.ErrorCode;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buffer, RX_BUF_SIZE);
#else
    g_rx_diagnostics.last_uart_error = huart1.ErrorCode;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma_buffer, RX_BUF_SIZE);
#endif
#endif
}

void RemoteBackend_Advance(uint32_t now_ms, RemoteSafetyInput_t safety)
{
    g_now_ms = now_ms;
#if REMOTE_BACKEND_A28_E34
    (void)safety;
    if (g_command.connected &&
        ((uint32_t)(now_ms - g_command.last_valid_ms) >=
         REMOTE_LEGACY_TIMEOUT_MS))
    {
        g_command.connected = false;
        g_command.link_fresh = false;
        g_command.stable_online = false;
        g_command.valid = false;
#if !defined(REMOTE_BACKEND_HOST)
        g_app.communication_connected = false;
        g_app.last_receive_timestamp = g_command.last_valid_ms;
#endif
    }
#elif REMOTE_BACKEND_FLYSKY_IBUS
    g_remote_safety = safety;
    RemoteBackend_RefreshFlySkyCommand(now_ms);
#if !defined(REMOTE_BACKEND_HOST)
    g_app.communication_connected = g_command.connected;
    g_app.last_receive_timestamp = g_command.last_valid_ms;
    RemoteBackend_ApplyCommand();
#endif
#endif
}

bool RemoteBackend_AdvanceGrassBag(bool permitted, GrassBagTarget_t *target)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && MOWER_GRASS_BAG_CH6_ENABLED
    if (target == NULL) return false;
    bool allowed = permitted && g_command.grass_bag_configured &&
        g_command.valid && g_command.link_fresh && g_command.stable_online &&
        g_command.run_permitted;
    bool event = GrassBagRemote_Advance(&g_grass_bag_remote, g_command.grass_bag_raw,
                                        g_command.frame_sequence, allowed, target);
    g_command.grass_bag_target = g_grass_bag_remote.stable_target;
    return event;
#else
    (void)permitted;
    (void)target;
    return false;
#endif
}

bool RemoteBackend_GetCommand(RemoteCommand_t *command)
{
    if (command == NULL)
    {
        return false;
    }

#if REMOTE_BACKEND_FLYSKY_IBUS
    RemoteBackend_RefreshFlySkyCommand(RemoteBackend_Now());
#endif
    *command = g_command;
    return g_command.valid;
}
#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && REMOTE_BACKEND_FLYSKY_IBUS
bool RemoteBackend_ReadIbusProbe(RemoteIbusProbe_t *probe)
{
    FlySkyIbusSnapshot_t snapshot;
    uint8_t i;

    if ((probe == NULL) ||
        !RemoteBackend_ReadPublishedFlySky(&snapshot))
    {
        return false;
    }
    RemoteBackend_ExpireFlySkySnapshot(&snapshot, RemoteBackend_Now());
    for (i = 0U; i < 4U; i++)
    {
        probe->channels[i] = snapshot.channels[10U + i];
    }
    probe->frame_sequence = snapshot.frame_sequence;
    probe->last_valid_ms = snapshot.last_valid_ms;
    probe->timeout_sequence = snapshot.timeout_sequence;
    probe->link_fresh = snapshot.link_fresh;
    return true;
}
#endif


bool RemoteBackend_IsConnected(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    FlySkyIbusSnapshot_t snapshot;
    if (!RemoteBackend_ReadPublishedFlySky(&snapshot)) return false;
    RemoteBackend_ExpireFlySkySnapshot(&snapshot, RemoteBackend_Now());
    return snapshot.link_fresh && snapshot.stable_online;
#else
    return g_command.connected;
#endif
}

uint32_t RemoteBackend_LastReceiveAgeMs(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    FlySkyIbusSnapshot_t snapshot;
    if (!RemoteBackend_ReadPublishedFlySky(&snapshot)) return UINT32_MAX;
    return (uint32_t)(RemoteBackend_Now() - snapshot.last_valid_ms);
#else
    if (!g_command.link_fresh && (g_command.last_valid_ms == 0U))
    {
        return UINT32_MAX;
    }
    return (uint32_t)(RemoteBackend_Now() - g_command.last_valid_ms);
#endif
}

bool RemoteBackend_UsesUart(uintptr_t uart_instance)
{
#if REMOTE_BACKEND_A28_E34
    return uart_instance == REMOTE_UART_USART3;
#elif REMOTE_BACKEND_FLYSKY_IBUS
    return uart_instance == REMOTE_UART_USART1;
#else
    return false;
#endif
}

bool RemoteBackend_LegacyTelemetryEnabled(void)
{
#if REMOTE_BACKEND_A28_E34
    return true;
#else
    return false;
#endif
}

void RemoteBackend_GetBladeConfirmDiagnostics(BladeConfirmCh6Diagnostics_t *diagnostics)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    BladeConfirmCh6Adapter_GetDiagnostics(&g_blade_confirm_adapter, diagnostics);
    if (diagnostics != NULL)
    {
        diagnostics->raw = g_command.blade_confirm_raw;
    }
#else
    if (diagnostics != NULL)
    {
        memset(diagnostics, 0, sizeof(*diagnostics));
    }
#endif
}

void RemoteBackend_SetActiveBladeRequest(uint32_t request_sequence)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    g_active_blade_request_sequence = request_sequence;
#else
    (void)request_sequence;
#endif
}

void RemoteBackend_ConsumeConfirmEvent(uint32_t event_sequence)
{
#if REMOTE_BACKEND_FLYSKY_IBUS
    BladeConfirmCh6Adapter_ConsumeEvent(&g_blade_confirm_adapter, event_sequence);
    g_command.has_confirm_event = false;
#else
    (void)event_sequence;
#endif
}
