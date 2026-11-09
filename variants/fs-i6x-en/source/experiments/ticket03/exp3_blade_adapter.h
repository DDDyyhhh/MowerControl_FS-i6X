#ifndef TICKET03_EXP3_BLADE_ADAPTER_H
#define TICKET03_EXP3_BLADE_ADAPTER_H

#include "exp3_probe.h"

#if EXP3_ENABLED
#include "blade_manual_control.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Exp3PwmEvent event;
    BladeManualState_t blade_state;
    uint16_t ch4_raw;
    uint32_t request_sequence;
    uint32_t ch4_epoch;
    uint32_t baseline_event_sequence;
    uint32_t last_consumed_event_sequence;
    bool gate_permitted;
} Exp3BladeInput;

typedef enum {
    EXP3_BLADE_ALLOW = 0,
    EXP3_BLADE_DENY_GATE = 1,
    EXP3_BLADE_DENY_STATE = 2,
    EXP3_BLADE_DENY_REQUEST = 3,
    EXP3_BLADE_DENY_CH4 = 4,
    EXP3_BLADE_DENY_TARGET = 5,
    EXP3_BLADE_DENY_GENERATION = 6,
    EXP3_BLADE_DENY_SEQUENCE = 7
} Exp3BladeReason;

typedef struct {
    bool inject_confirm;
    bool invalidate;
    uint8_t reason;
    uint32_t request_sequence;
    uint32_t event_sequence;
} Exp3BladeDecision;

Exp3BladeDecision exp3_blade_adapter_decide(const Exp3BladeInput *input);

#ifdef __cplusplus
}
#endif
#endif
#endif
