#ifndef EXP3_BOARD_H
#define EXP3_BOARD_H
#include "exp3_hil.h"
#include "mower_control_contract.h"
#ifndef EXP3_PWM_PROBE_ENABLED
#define EXP3_PWM_PROBE_ENABLED 0
#endif
#ifndef EXP3_AUTO_SESSION_ENABLED
#define EXP3_AUTO_SESSION_ENABLED 0
#endif
#if EXP3_PWM_PROBE_ENABLED != 0 && EXP3_PWM_PROBE_ENABLED != 1
#error "EXP3_PWM_PROBE_ENABLED must be 0 or 1"
#endif
#if EXP3_PWM_PROBE_ENABLED && !EXP3_ENABLED
#error "EXP3_PWM_PROBE_ENABLED requires EXP3_ENABLED"
#endif
#if EXP3_AUTO_SESSION_ENABLED && !EXP3_PWM_PROBE_ENABLED
#error "EXP3_AUTO_SESSION_ENABLED requires EXP3_PWM_PROBE_ENABLED"
#endif
#if EXP3_ENABLED
#include "exp3_probe.h"
#if EXP3_AUTO_SESSION_ENABLED
#include "exp3_auto_session.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern Exp3Hil g_exp3_hil;
extern volatile Exp3Mailbox g_exp3_mailbox;
extern volatile uint16_t g_exp3_downlink_word;
extern volatile uint32_t g_exp3_uplink_channels;
extern volatile uint32_t g_exp3_capture_overflow;
typedef struct {
    Exp3Message last_result;
    uint32_t controller;
    uint32_t hmi;
    uint32_t matches;
    uint8_t bound;
    uint8_t waiting;
    uint8_t cancel_ready;
    uint8_t cancel_failed;
} Exp3BoardStatus;
typedef struct {
    uint32_t action;
    uint32_t request;
    uint32_t generation;
    uint32_t ch4_epoch;
    uint32_t pending_sequence;
    uint32_t pwm_compare;
    uint32_t confirm_count;
    uint32_t reject_count;
    uint16_t ch4_raw;
    uint8_t result_status;
    uint8_t blade_state;
    uint8_t gate_reason;
    uint8_t last_event;
    uint8_t decision_reason;
} Exp3PwmDiag;
void exp3_board_sens(const uint8_t *request, const uint8_t *response, unsigned length);
void exp3_board_capture_rf(const uint8_t *packet, unsigned length, uint32_t now);
void exp3_board_poll(uint32_t now);
#if EXP3_AUTO_SESSION_ENABLED
void exp3_board_auto_start(uint32_t seed, uint32_t now);
#endif
void exp3_board_channels(uint32_t now, uint16_t ch4, uint16_t ch11, uint16_t ch12);
void exp3_board_rf(const uint8_t *packet, unsigned length, uint32_t now);
void exp3_board_status(Exp3BoardStatus *status);
bool exp3_board_submit(const Exp3Message *message);
#if EXP3_PWM_PROBE_ENABLED
bool exp3_board_take_pwm_event(Exp3PwmEvent *event);
#if MOWER_BLADE_OK_ENABLED
extern volatile uint16_t g_blade_ok_runtime_word;
extern volatile uint16_t g_mower_unlock_generation, g_mower_control_word;
bool exp3_board_take_chassis_event(Exp3PwmEvent *event);
void exp3_board_request_rebind(void);
bool exp3_board_control(uint32_t now, uint16_t *generation, uint16_t *status);
void exp3_board_complete_pwm_event(const Exp3PwmEvent *event, bool accepted);
bool exp3_board_defer_result(const Exp3Message *result);
bool exp3_board_runtime(uint32_t now, uint16_t *word);
/* True only for a live, progressing PD10 sample; connected is not a position. */
bool exp3_board_bag_connection(uint32_t now, bool *connected);
#endif
uint32_t exp3_board_ch4_epoch(void);
void exp3_board_publish_pwm_diag(const Exp3PwmDiag *diag);
void exp3_board_read_pwm_diag(Exp3PwmDiag *diag);
#endif
#ifdef __cplusplus
}
#endif
#endif
#endif
