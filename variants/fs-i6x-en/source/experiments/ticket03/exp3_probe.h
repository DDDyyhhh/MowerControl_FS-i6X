#ifndef TICKET03_EXP3_PROBE_H
#define TICKET03_EXP3_PROBE_H

#include "exp3.h"

#if EXP3_ENABLED
#ifdef __cplusplus
extern "C" {
#endif

#define EXP3_PWM_CONTEXT 0x424c4144UL
#define EXP3_PWM_TARGET 1U
#define EXP3_UNLOCK_CONTEXT 0x554e4c4bUL
#define EXP3_UNLOCK_TARGET 1U

typedef enum {
    EXP3_UI_IDLE = 0,
    EXP3_UI_PROMPT,
    EXP3_UI_WAIT,
    EXP3_UI_ACCEPTED,
    EXP3_UI_REJECTED,
    EXP3_UI_TIMEOUT,
    EXP3_UI_UNBOUND,
    EXP3_UI_CANCEL_WAIT,
    EXP3_UI_BATTERY_LOW, /* Local presentation only; never a wire transaction. */
    EXP3_UI_NOT_READY, EXP3_UI_STICKS_REQUIRED, EXP3_UI_CENTER_REQUIRED,
    EXP3_UI_SYNCHRONIZING, EXP3_UI_SAFETY, EXP3_UI_OWNER
} Exp3UiState;

typedef enum {
    EXP3_UI_SUBMIT_NONE = 0,
    EXP3_UI_SUBMIT_CONFIRM,
    EXP3_UI_SUBMIT_CANCEL
} Exp3UiSubmitKind;

typedef struct {
    uint16_t ch4_us;
    uint8_t ok_pressed;
    uint8_t start_inhibited; /* Observe physical edges, but do not arm a new intent. */
    uint8_t bound;
    uint8_t authority_valid;
    uint16_t authority;
    uint8_t transport_waiting;
    uint8_t cancel_ready;
    uint32_t now_ms;
    uint32_t matches;
    Exp3Message last_result;
} Exp3UiInput;

typedef struct {
    Exp3UiState state;
    Exp3UiSubmitKind submit_kind;
    Exp3Message message;
} Exp3UiOutput;

typedef struct {
    Exp3UiState state;
    uint8_t center_seen;
    uint8_t cancel_pending;
    uint8_t ch4_start;
    uint8_t start_armed;
    uint8_t completed;
    uint32_t controller;
    uint32_t hmi;
    uint32_t context;
    uint32_t next_generation;
    uint32_t prompt_generation;
    uint32_t next_action;
    uint32_t sent_at;
    uint32_t edge_at;
    uint32_t seen_matches;
    uint16_t runtime_baseline;
    uint16_t target;
    Exp3Message pending;
} Exp3UiProbe;

typedef enum {
    EXP3_PWM_NONE = 0,
    EXP3_PWM_CONFIRM,
    EXP3_PWM_CANCEL
} Exp3PwmEventKind;

typedef struct {
    Exp3PwmEventKind kind;
    Exp3Message message;
    uint32_t ch4_epoch;
} Exp3PwmEvent;

typedef struct {
    Exp3Message records[EXP3_CACHE_SIZE];
    uint8_t flags[EXP3_CACHE_SIZE];
    uint8_t next;
    uint32_t highest_generation;
    uint32_t highest_action;
} Exp3PwmProbe;

void exp3_ui_probe_init(Exp3UiProbe *probe);
void exp3_ui_probe_bind(Exp3UiProbe *probe, uint32_t controller, uint32_t hmi,
                        uint32_t context, uint16_t target, uint32_t generation);
Exp3UiOutput exp3_ui_probe_step(Exp3UiProbe *probe, const Exp3UiInput *input);
void exp3_pwm_probe_init(Exp3PwmProbe *probe);
Exp3PwmEvent exp3_pwm_probe_observe(Exp3PwmProbe *probe, const Exp3Message *result);

#ifdef __cplusplus
}
#endif
#endif
#endif
