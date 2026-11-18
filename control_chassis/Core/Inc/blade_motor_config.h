#ifndef BLADE_MOTOR_CONFIG_H
#define BLADE_MOTOR_CONFIG_H

#include "../../../experiments/ticket03/blade_gear_presets.h"

#ifdef BLADE_MOTOR_DRIVER_PWM
#error "BLADE_MOTOR_DRIVER_PWM is a reserved driver type constant"
#endif
#ifdef BLADE_MOTOR_DRIVER_FTESC_CAN
#error "BLADE_MOTOR_DRIVER_FTESC_CAN is a reserved driver type constant"
#endif
#ifdef BLADE_MOTOR_DRIVER_FLIPSKY_CAN
#error "BLADE_MOTOR_DRIVER_FLIPSKY_CAN is obsolete; use BLADE_MOTOR_DRIVER_FTESC_CAN"
#endif

#define BLADE_MOTOR_DRIVER_PWM        1U
#define BLADE_MOTOR_DRIVER_FTESC_CAN  2U

#ifndef BLADE_MOTOR_DRIVER_USED
#define BLADE_MOTOR_DRIVER_USED BLADE_MOTOR_DRIVER_FTESC_CAN
#endif

#if (BLADE_MOTOR_DRIVER_USED != BLADE_MOTOR_DRIVER_PWM) && \
    (BLADE_MOTOR_DRIVER_USED != BLADE_MOTOR_DRIVER_FTESC_CAN)
#error "BLADE_MOTOR_DRIVER_USED must select PWM or FTESC CAN"
#endif

/* PWM adapter settings: input and output are duty-cycle percentages. */
#define BLADE_PWM_DUTY_DEADZONE_PERCENT  0.01f
#define BLADE_PWM_DUTY_MIN_PERCENT       45.0f
#define BLADE_PWM_DUTY_MAX_PERCENT       55.0f
#define BLADE_PWM_DUTY_LEVEL_1_PERCENT   45.0f
#define BLADE_PWM_DUTY_LEVEL_2_PERCENT   50.0f
#define BLADE_PWM_DUTY_LEVEL_3_PERCENT   55.0f

#define BLADE_FTESC_KEEPALIVE_MS  200U
/* Stop = ERPM 0 brake for this long, then zero-current release while idle.
 * A sensorless ESC held at ERPM 0 can twitch the blade on lifter EMI/jolts. */
#ifndef BLADE_FTESC_STOP_BRAKE_MS
#define BLADE_FTESC_STOP_BRAKE_MS  3000U
#endif
#if (BLADE_FTESC_STOP_BRAKE_MS) == 0U
#error "BLADE_FTESC_STOP_BRAKE_MS must be greater than zero"
#endif
/* 1: idle blade is released after the brake window; 0: legacy ERPM 0 hold. */
#ifndef BLADE_FTESC_IDLE_RELEASE_ENABLED
#define BLADE_FTESC_IDLE_RELEASE_ENABLED  1
#endif
#if (BLADE_FTESC_IDLE_RELEASE_ENABLED != 0) && (BLADE_FTESC_IDLE_RELEASE_ENABLED != 1)
#error "BLADE_FTESC_IDLE_RELEASE_ENABLED must be 0 or 1"
#endif
/* 1: idle release is 0x02 duty 0 (with Zero Duty Mode = Release Motor);
 * 0: legacy 0x15 current 0.  Duty 0 asks the ESC to drop its output stage,
 * while current 0 can leave the current loop enabled and twitch on EMI. */
#ifndef BLADE_FTESC_RELEASE_USES_DUTY
#define BLADE_FTESC_RELEASE_USES_DUTY  1
#endif
#if (BLADE_FTESC_RELEASE_USES_DUTY != 0) && (BLADE_FTESC_RELEASE_USES_DUTY != 1)
#error "BLADE_FTESC_RELEASE_USES_DUTY must be 0 or 1"
#endif
/* Wire sign only: callers keep using positive ERPM as "run" intent. */
#ifndef BLADE_FTESC_ERPM_DIRECTION
#define BLADE_FTESC_ERPM_DIRECTION  (1)
#endif
#if (BLADE_FTESC_ERPM_DIRECTION != 1) && (BLADE_FTESC_ERPM_DIRECTION != -1)
#error "BLADE_FTESC_ERPM_DIRECTION must be 1 or -1"
#endif

/* FTESC protocol settings. Change the ID to match ESC Tool configuration. */
#define FLIPSKY_ESC_ID                  154U
#define FLIPSKY_CAN_BITRATE             500000U
#define FLIPSKY_CAN_CMD_DUTY_GEAR_CRC16 0x02U /* duty + gear, CRC16 protected */
#define FLIPSKY_CAN_CMD_CURRENT_PERCENT 0x03U
#define FLIPSKY_CAN_CMD_CURRENT_GEAR    0x15U /* current + gear, no CRC; 0 = release */
#define FLIPSKY_CAN_CMD_ERPM_GEAR       0x18U /* erpm + gear, no CRC */
#define FLIPSKY_CAN_CMD_REALTIME_0      0x0BU
#define FLIPSKY_CAN_CMD_REALTIME_1      0x0CU
#define FLIPSKY_CAN_CMD_REALTIME_2      0x0DU
#define FLIPSKY_CAN_RX_TIMEOUT_MS       500U

/* Keep Blade RPM unavailable until Ticket 09 hardware capture is accepted. */
#define FLIPSKY_REALTIME_FEEDBACK_VALIDATED 0U

#if FLIPSKY_REALTIME_FEEDBACK_VALIDATED != 0U
#error "FLIPSKY_REALTIME_FEEDBACK_VALIDATED must remain 0 until Ticket 09 passes"
#endif

#endif
