#ifndef BLADE_MOTOR_CONFIG_H
#define BLADE_MOTOR_CONFIG_H

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
#define BLADE_MOTOR_DRIVER_USED BLADE_MOTOR_DRIVER_PWM
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

/* Existing hardware-validated FTESC ERPM levels; do not retune here. */
#define BLADE_FTESC_ERPM_LEVEL_1  14000.0f
#define BLADE_FTESC_ERPM_LEVEL_2  15500.0f
#define BLADE_FTESC_ERPM_LEVEL_3  16000.0f
#define BLADE_FTESC_KEEPALIVE_MS  200U

/* FTESC protocol settings. Change the ID to match ESC Tool configuration. */
#define FLIPSKY_ESC_ID                  154U
#define FLIPSKY_CAN_BITRATE             500000U
#define FLIPSKY_CAN_CMD_CURRENT_PERCENT 0x03U
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
