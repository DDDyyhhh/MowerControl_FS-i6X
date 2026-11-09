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
#if REMOTE_BACKEND_FLYSKY_IBUS
static FlySkyIbusServo_t g_flysky_ibus;
static RemoteAuthorization_t g_remote_authorization;
#if MOWER_BLADE_OK_ENABLED
static MowerModeControl_t g_mode_control;
MowerModeControl_t *RemoteBackend_ModeControl(void) { return &g_mode_control; }
#endif
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

#if REMOTE_BACKEND_FLYSKY_IBUS
static void RemoteBackend_UpdateAuthorization(const FlySkyIbusSnapshot_t *snapshot,
                                              RemoteSafetyInput_t safety,
                                              uint32_t now_ms)
{
    RemoteAuthorizationInput_t input;

    input.link_fresh = snapshot->link_fresh;
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
    input.frame_sequence = snapshot->frame_sequence;
    input.timeout_sequence = snapshot->timeout_sequence;
#if MOWER_BLADE_OK_ENABLED
    MowerModeInput_t mode = {0};
    mode.online = snapshot->link_fresh && snapshot->stable_online;
    mode.enabled = input.run_enable_raw >= 1880U && input.run_enable_raw <= 2120U;
    mode.safety_active = safety.emergency_active || safety.collision_active || safety.tilt_active;
    mode.centered = input.steering_raw >= 1450U && input.steering_raw <= 1550U &&
        input.throttle_raw >= 1450U && input.throttle_raw <= 1550U;
    mode.blade_centered = snapshot->blade_start_raw >= 1400U && snapshot->blade_start_raw <= 1600U;
    mode.mode_raw = snapshot->channels[REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_MODE)];
    mode.frame_sequence = snapshot->frame_sequence;
    MowerMode_Advance(&g_mode_control, &mode, now_ms);
    input.unlock_raw = 1500U; /* CH8 has no unlock semantics in production. */
#endif
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
#if MOWER_BLADE_OK_ENABLED
    authorization.run_permitted = g_mode_control.permitted;
    authorization.remote_recoverable_latched = !g_mode_control.armed;
    authorization.command_invalidation_sequence = g_mode_control.invalidation_sequence;
    authorization.state = g_mode_control.armed ? REMOTE_AUTH_MANUAL_ARMED : REMOTE_AUTH_STOPPED_LATCHED;
    authorization.latch_reason = g_mode_control.latch_reason;
#endif
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
#if MOWER_BLADE_OK_ENABLED
    g_command.mode_raw = snapshot->channels[REMOTE_IBUS_CHANNEL_INDEX(REMOTE_IBUS_CHANNEL_MODE)];
    g_command.selected_mode = (uint8_t)g_mode_control.selected;
    g_command.mode_valid = g_mode_control.mode_valid;
    g_command.mode_sequence = g_mode_control.mode_sequence;
#endif
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
    g_command.remote_run_enable = authorization.run_enable == REMOTE_SWITCH_ON;
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

#if !MOWER_BLADE_OK_ENABLED
static void RemoteBackend_RefreshFlySkyCommand(void)
{
    FlySkyIbusSnapshot_t snapshot;
    bool available;

#if !defined(REMOTE_BACKEND_HOST)
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
#endif
    available = FlySkyIbusServo_ReadSnapshot(&g_flysky_ibus, &snapshot);
#if !defined(REMOTE_BACKEND_HOST)
    if (interrupt_mask == 0U)
    {
        __enable_irq();
    }
#endif
    if (available)
    {
        RemoteBackend_UpdateAuthorization(&snapshot, g_remote_safety,
                                          RemoteBackend_Now());
        RemoteBackend_CopyFlySkySnapshot(&snapshot);
    }
}
#endif
#endif

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
        g_app.lock = 0U;
        g_app.physical_safety_latched = 0U;
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

static void RemoteBackend_HandleLegacy(const uint8_t *data, uint16_t length)
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
            RemoteBackend_PublishLegacy(RemoteBackend_Now());
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
#if REMOTE_BACKEND_FLYSKY_IBUS
    g_active_blade_request_sequence = 0U;
    memset(&g_grass_bag_remote, 0, sizeof(g_grass_bag_remote));
    memset(&g_remote_safety, 0, sizeof(g_remote_safety));
    FlySkyIbusServo_Init(&g_flysky_ibus, config);
    RemoteAuthorization_Init(&g_remote_authorization, config);
#if MOWER_BLADE_OK_ENABLED
    MowerMode_Init(&g_mode_control);
#endif
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

void RemoteBackend_Process(void)
{
#if REMOTE_BACKEND_A28_E34 && !defined(REMOTE_BACKEND_HOST)
    UartRxMsg_t msg;
    if (osMessageQueueGet(Queue_RxDataHandle, &msg, NULL, 2000U) == osOK)
    {
        RemoteBackend_HandleLegacy(msg.data, msg.len);
        RemoteBackend_ApplyCommand();
    }
    else
    {
        HAL_UART_AbortReceive(&huart3);
        HAL_UART_DMAStop(&huart3);
        __HAL_UART_CLEAR_OREFLAG(&huart3);
        __HAL_UART_CLEAR_NEFLAG(&huart3);
        __HAL_UART_CLEAR_FEFLAG(&huart3);
        (void)huart3.Instance->DR;
        HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buffer, RX_BUF_SIZE);
    }
#elif REMOTE_BACKEND_FLYSKY_IBUS && !defined(REMOTE_BACKEND_HOST)
    UartRxMsg_t msg;
    if (osMessageQueueGet(Queue_RxDataHandle, &msg, NULL, 10U) == osOK)
    {
        FlySkyIbusServo_Feed(&g_flysky_ibus, msg.data, msg.len,
                             RemoteBackend_Now());
    }
    /* The 100 Hz motion task advances authorization with current safety inputs. */
#endif
}

void RemoteBackend_OnUartRx(uintptr_t uart_instance,
                            const uint8_t *data,
                            uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        !RemoteBackend_UsesUart(uart_instance))
    {
        return;
    }

#if REMOTE_BACKEND_A28_E34
#if !defined(REMOTE_BACKEND_HOST)
    UartRxMsg_t msg;
    msg.len = (length > RX_BUF_SIZE) ? RX_BUF_SIZE : length;
    memcpy(msg.data, data, msg.len);
    (void)osMessageQueuePut(Queue_RxDataHandle, &msg, 0U, 0U);
#else
    RemoteBackend_HandleLegacy(data, length);
#endif
#elif REMOTE_BACKEND_FLYSKY_IBUS
#if !defined(REMOTE_BACKEND_HOST)
    UartRxMsg_t msg;
    msg.len = (length > RX_BUF_SIZE) ? RX_BUF_SIZE : length;
    memcpy(msg.data, data, msg.len);
    (void)osMessageQueuePut(Queue_RxDataHandle, &msg, 0U, 0U);
#else
    FlySkyIbusServo_Feed(&g_flysky_ibus, data, length, RemoteBackend_Now());
#endif
#endif
}

void RemoteBackend_OnUartError(uintptr_t uart_instance)
{
#if !defined(REMOTE_BACKEND_HOST)
    if (!RemoteBackend_UsesUart(uart_instance))
    {
        return;
    }
#if REMOTE_BACKEND_A28_E34
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buffer, RX_BUF_SIZE);
#elif REMOTE_BACKEND_FLYSKY_IBUS
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma_buffer, RX_BUF_SIZE);
#endif
#else
    (void)uart_instance;
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
    FlySkyIbusSnapshot_t snapshot;

    g_remote_safety = safety;
    FlySkyIbusServo_Advance(&g_flysky_ibus, now_ms);
    if (FlySkyIbusServo_ReadSnapshot(&g_flysky_ibus, &snapshot))
    {
        RemoteBackend_UpdateAuthorization(&snapshot, safety, now_ms);
        RemoteBackend_CopyFlySkySnapshot(&snapshot);
    }
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

#if REMOTE_BACKEND_FLYSKY_IBUS && !MOWER_BLADE_OK_ENABLED
    RemoteBackend_RefreshFlySkyCommand();
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
        !FlySkyIbusServo_ReadSnapshot(&g_flysky_ibus, &snapshot))
    {
        return false;
    }
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
#if REMOTE_BACKEND_FLYSKY_IBUS && !MOWER_BLADE_OK_ENABLED
    RemoteBackend_RefreshFlySkyCommand();
#endif
    return g_command.connected;
}

uint32_t RemoteBackend_LastReceiveAgeMs(void)
{
#if REMOTE_BACKEND_FLYSKY_IBUS && !MOWER_BLADE_OK_ENABLED
    RemoteBackend_RefreshFlySkyCommand();
#endif
    if (!g_command.link_fresh && (g_command.last_valid_ms == 0U))
    {
        return UINT32_MAX;
    }
    return (uint32_t)(RemoteBackend_Now() - g_command.last_valid_ms);
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
