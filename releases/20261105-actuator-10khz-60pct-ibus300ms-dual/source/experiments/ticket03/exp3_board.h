#ifndef EXP3_BOARD_H
#define EXP3_BOARD_H
#include "exp3_hil.h"
#include "mower_fast_status.h"
#include "exp3_completion.h"
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
    uint16_t ch4_raw, steering_raw, throttle_raw, authority;
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
void exp3_board_complete_pwm_event(const Exp3PwmEvent *event, bool accepted);
bool exp3_board_defer_result(const Exp3Message *result);
/* Nonblocking, atomic 32-bit SENS observation; zero means no completion. */
uint32_t exp3_board_completion_part(unsigned part);
bool exp3_board_runtime(uint32_t now, uint16_t *word);
bool exp3_board_authority(uint32_t now, uint16_t *word);
void exp3_board_restart_session(uint32_t now);
/* Motion owns allocation and consumption; RF/SENS only transport snapshots. */
uint16_t exp3_board_update_intent(unsigned phase, bool unlock, uint32_t request, uint32_t now);
bool exp3_board_check_intent(const Exp3Message *message, uint32_t now);
/* True only for a live, progressing PD10 sample; connected is not a position. */
bool exp3_board_bag_connection(uint32_t now, bool *connected);
/* Confirmed full state; false for legacy, disconnected or stale feedback. */
bool exp3_board_bag_full(uint32_t now, bool *full);
/* Read-only, fresh controller height estimate in millimetres. */
bool exp3_board_height(uint32_t now, uint16_t *mm);
/* Idle E1 diagnostics; false when unbound, incomplete or no heartbeat. */
bool exp3_board_battery(uint32_t now, uint16_t *minimum_mv, uint16_t *reasons);
#endif
uint32_t exp3_board_ch4_epoch(void);
uint32_t exp3_board_unlock_epoch(void);
void exp3_board_publish_pwm_diag(const Exp3PwmDiag *diag);
void exp3_board_read_pwm_diag(Exp3PwmDiag *diag);
#endif
#ifdef __cplusplus
}
#endif
#endif
#endif
