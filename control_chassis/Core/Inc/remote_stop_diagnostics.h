#ifndef REMOTE_STOP_DIAGNOSTICS_H
#define REMOTE_STOP_DIAGNOSTICS_H

#include <stdint.h>
#include "remote_backend.h"

#define REMOTE_STOP_DIAG_MAGIC   0x52534431U /* RSD1 */
#define REMOTE_STOP_DIAG_VERSION 1U

/* Motion is the only writer. A permitted cycle arms the next falling edge;
 * subsequent denied cycles cannot overwrite that episode's first record.
 * ST-Link reads after the real stop, with the CPU halted. An odd sequence is
 * an interrupted/incomplete publication and must not be accepted. */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    volatile uint32_t write_sequence;
    uint32_t event_count;
    uint32_t armed;
    uint32_t valid;
    uint32_t boot_reset_flags;
    uint32_t authorized_ms;
    uint32_t authorized_frame_sequence;
    uint32_t authorized_timeout_sequence;
    uint32_t authorized_ch5_stop_sequence;
    RemoteRxDiagnostics_t rx_at_authorization;
    uint32_t captured_ms;
    uint32_t gate_reason;
    uint32_t authorization_state;
    uint32_t latch_reason;
    uint32_t last_valid_ms;
    uint32_t link_age_ms;
    uint32_t frame_sequence;
    uint32_t timeout_sequence;
    uint32_t ch5_stop_sequence;
    uint32_t invalidation_sequence;
    uint32_t control_mode;
    uint32_t report_state;
    /* bits: connected/fresh/stable/history/timeout-stop/remote-latch/run,
     * software-lock/physical-latch/input-physical/emergency/collision/tilt,
     * collision-latch/collision-enabled/tilt-enabled. */
    uint32_t input_flags;
    uint32_t raw_gpiod;
    uint32_t requested_height_level;
    uint32_t remote_height_level;
    uint32_t lifter_pwm_ch3;
    uint32_t lifter_pwm_ch4;
    uint32_t lifter_pwm_period;
    float pitch;
    float roll;
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    RemoteRxDiagnostics_t rx;
} RemoteStopDiagnostics_t;

#endif
