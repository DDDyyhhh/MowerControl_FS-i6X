#ifndef BLADE_HMI_CONTRACT_H
#define BLADE_HMI_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file blade_hmi_contract.h
 * @brief Frozen Logical Contract between STM32 (control_chassis) and OpenI6X (HMI)
 *
 * This contract defines the logical messages exchanged for Blade Manual Start
 * Confirmation. STM32 -> OpenI6X prompt transport remains deferred / uncommitted
 * (candidate SENS address 9 to be evaluated in Ticket 11).
 * OpenI6X -> STM32 confirmation is carried via the CH6 event adapter.
 *
 * Wire format (little-endian):
 * 1. BladeStartConfirmRequest:
 *    - uint8_t  version (must be BLADE_PROTOCOL_VERSION = 1)
 *    - uint8_t  state   (must be BLADE_STATE_START_CONFIRM_PENDING = 1)
 *    - uint32_t request_sequence (> 0)
 * 2. BladeConfirmOK:
 *    - uint8_t  version (must match BLADE_PROTOCOL_VERSION = 1)
 *    - uint32_t request_sequence (must match pending request_sequence)
 *    - uint32_t event_sequence   (monotonically increasing > 0, single-use)
 */

#define BLADE_PROTOCOL_VERSION 1U

typedef enum
{
    BLADE_STATE_IDLE = 0,
    BLADE_STATE_START_CONFIRM_PENDING = 1,
    BLADE_STATE_RUNNING = 2,
    BLADE_STATE_STOPPING = 3
} BladeManualState_t;

/**
 * @brief STM32 -> OpenI6X Logical Message: BladeStartConfirmRequest
 *
 * Invariant: Valid only while state is BLADE_STATE_START_CONFIRM_PENDING.
 */
typedef struct
{
    uint8_t version;
    BladeManualState_t state;
    uint32_t request_sequence;
} BladeStartConfirmRequest_t;

/**
 * @brief OpenI6X -> STM32 Logical Message: BladeConfirmOK
 *
 * Invariant: Must match pending request_sequence, carry a fresh event_sequence,
 * and be consumed at most once.
 */
typedef struct
{
    uint8_t version;
    uint32_t request_sequence;
    uint32_t event_sequence;
} BladeConfirmOK_t;

/**
 * @brief Invariant validation for BladeStartConfirmRequest
 */
static inline bool BladeHmi_IsStartConfirmRequestValid(const BladeStartConfirmRequest_t *request)
{
    if (request == NULL)
    {
        return false;
    }
    return (request->version == BLADE_PROTOCOL_VERSION) &&
           (request->state == BLADE_STATE_START_CONFIRM_PENDING) &&
           (request->request_sequence > 0U);
}

/**
 * @brief Invariant validation for BladeConfirmOK
 *
 * Rejects unsupported versions, mismatched request_sequences, non-fresh events,
 * and already consumed / duplicate events.
 */
static inline bool BladeHmi_IsConfirmOKValid(const BladeConfirmOK_t *ok,
                                            uint32_t expected_request_sequence,
                                            uint32_t baseline_event_sequence,
                                            uint32_t last_consumed_event_sequence)
{
    if (ok == NULL)
    {
        return false;
    }
    if (ok->version != BLADE_PROTOCOL_VERSION)
    {
        return false; /* Unsupported version */
    }
    if ((expected_request_sequence == 0U) ||
        (ok->request_sequence != expected_request_sequence))
    {
        return false; /* Mismatched request sequence or not pending */
    }
    if ((int32_t)(ok->event_sequence - baseline_event_sequence) <= 0)
    {
        return false; /* Not fresh: generated before or at baseline */
    }
    if ((last_consumed_event_sequence != 0U) &&
        ((int32_t)(ok->event_sequence - last_consumed_event_sequence) <= 0))
    {
        return false; /* Duplicate / already consumed */
    }
    return true;
}

#endif /* BLADE_HMI_CONTRACT_H */
