#ifndef REMOTE_BACKEND_H
#define REMOTE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "remote_backend_config.h"
#if MOWER_BLADE_OK_ENABLED
#include "mower_mode_control.h"
MowerModeControl_t *RemoteBackend_ModeControl(void); /* Motion task only. */
#endif
#include "grass_bag_control.h"
#include "remote_authorization.h"
#include "blade_confirm_ch6_adapter.h"

typedef struct
{
    uint16_t steering;
    uint16_t throttle;
    uint16_t blade_height;
    uint16_t travel_speed_raw;
    float travel_speed_mps;
    uint16_t blade_gear_raw;
    uint16_t run_enable_raw;
    uint16_t unlock_raw;
    uint16_t mode_raw;
    uint8_t selected_mode;
    bool mode_valid;
    uint32_t mode_sequence;
    uint16_t blade_start_raw;
    uint16_t blade_confirm_raw;
    uint16_t grass_bag_raw;
    GrassBagTarget_t grass_bag_target;
    bool grass_bag_configured;
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    uint8_t keys[8];
    uint8_t gear;
    uint8_t blade_gear;
    uint8_t blade_speed;
    uint8_t height_level;
    uint8_t operation_mode;
    uint8_t config_flags;
    bool remote_run_enable;
    uint8_t remote_unlock;
    bool receiver_failsafe;
    bool remote_recoverable_latched;
    bool run_permitted;
    bool connected;
    bool valid;
    bool link_fresh;
    bool stable_online;
    bool timeout_latched;
    bool run_enable_configured;
    bool blade_start_configured;
    bool blade_confirm_configured;
    bool blade_height_configured;
    bool travel_speed_configured;
    bool has_confirm_event;
    uint32_t confirm_request_sequence;
    uint32_t confirm_event_sequence;
    uint32_t frame_sequence;
    uint32_t timeout_sequence;
    uint32_t command_invalidation_sequence;
    RemoteAuthorizationState_t authorization_state;
    RemoteRecoverableLatchReason_t recoverable_latch_reason;
    uint32_t last_valid_ms;
} RemoteCommand_t;

typedef struct
{
    bool physical_latched;
    bool emergency_active;
    bool collision_active;
    bool tilt_active;
} RemoteSafetyInput_t;

typedef uint32_t (*RemoteNowMsFn)(void *context);

typedef struct
{
    RemoteNowMsFn now_ms;
    void *context;
} RemoteBackendClock_t;

FlySkyIbusConfig_t RemoteBackend_DefaultFlySkyConfig(void);
float RemoteBackend_MapTravelSpeed(uint16_t pulse_us);
float RemoteBackend_ApplyReverseSpeed(float speed_mps, bool reverse);
void RemoteBackend_Init(RemoteBackendClock_t clock);
void RemoteBackend_InitWithFlySkyConfig(RemoteBackendClock_t clock,
                                        const FlySkyIbusConfig_t *config);
/* Single consumer: Motion supplies the complete permission including mode/connection. */
bool RemoteBackend_AdvanceGrassBag(bool permitted, GrassBagTarget_t *target);
void RemoteBackend_Start(void);
void RemoteBackend_Process(void);
void RemoteBackend_OnUartRx(uintptr_t uart_instance, const uint8_t *data, uint16_t length);
void RemoteBackend_OnUartError(uintptr_t uart_instance);
void RemoteBackend_Advance(uint32_t now_ms, RemoteSafetyInput_t safety);
bool RemoteBackend_GetCommand(RemoteCommand_t *command);
bool RemoteBackend_IsConnected(void);
uint32_t RemoteBackend_LastReceiveAgeMs(void);
bool RemoteBackend_UsesUart(uintptr_t uart_instance);
bool RemoteBackend_LegacyTelemetryEnabled(void);
void RemoteBackend_GetJoystickOutput(uint16_t joystick[4]);
void RemoteBackend_ConsumeConfirmEvent(uint32_t event_sequence);
void RemoteBackend_GetBladeConfirmDiagnostics(BladeConfirmCh6Diagnostics_t *diagnostics);
void RemoteBackend_SetActiveBladeRequest(uint32_t request_sequence);

#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED && REMOTE_BACKEND_FLYSKY_IBUS
typedef struct
{
    uint16_t channels[4]; /* CH11..CH14, raw SERVO microseconds */
    uint32_t frame_sequence;
    uint32_t last_valid_ms;
    uint32_t timeout_sequence;
    bool link_fresh;
} RemoteIbusProbe_t;
bool RemoteBackend_ReadIbusProbe(RemoteIbusProbe_t *probe);
#endif

#endif
