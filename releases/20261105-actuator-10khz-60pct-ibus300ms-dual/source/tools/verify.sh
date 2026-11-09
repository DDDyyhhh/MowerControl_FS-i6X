#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOG_DIR="${REPO_ROOT}/build/logs"
WORK_DIR=$(mktemp -d /tmp/mower_verify_XXXXXX)
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p "$LOG_DIR"

# Text formatters
BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

pass() {
    echo -e "  [${GREEN}PASS${NC}] $1"
}

fail() {
    echo -e "  [${RED}FAIL${NC}] $1" >&2
}

info() {
    echo -e "${BOLD}$1${NC}"
}

run_host_harnesses() {
    info "=== Running Battery Protection Harnesses ==="
    local battery_log="${LOG_DIR}/host_battery_protection.log"
    python3 "${REPO_ROOT}/tools/test_battery_protection.py" >"$battery_log" 2>&1 || {
        fail "Battery Protection Harnesses"
        cat "$battery_log" >&2
        return 1
    }
    pass "Battery Protection (BMS publication, Motion recovery, full-speed wheel adapters and LOW latch)"
    info "=== [1/9] Running Blade Driver Selection Harness ==="
    local log="${LOG_DIR}/host_blade_driver_selection.log"
    local cc=${CC:-gcc}
    local common=(-std=c99 -Wall -Wextra -Werror -I"${REPO_ROOT}/control_chassis/host/fakes" -I"${REPO_ROOT}/control_chassis/Core/Inc")
    local sources=(
        "${REPO_ROOT}/control_chassis/host/blade_driver_selection_harness.c"
        "${REPO_ROOT}/control_chassis/Core/UserDev/BladeMotor.c"
        "${REPO_ROOT}/control_chassis/Core/UserApp/safety_ownership_gate.c"
    )

    {
        "$cc" "${common[@]}" -DEXPECTED_BLADE_DRIVER=2U "${sources[@]}" -lm -o "${WORK_DIR}/blade_driver_default" || return 1
        "${WORK_DIR}/blade_driver_default" || return 1
        "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=1U -DEXPECTED_BLADE_DRIVER=1U "${sources[@]}" -lm -o "${WORK_DIR}/blade_driver_pwm" || return 1
        "${WORK_DIR}/blade_driver_pwm" || return 1
        "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=2U -DEXPECTED_BLADE_DRIVER=2U "${sources[@]}" -lm -o "${WORK_DIR}/blade_driver_ftesc" || return 1
        "${WORK_DIR}/blade_driver_ftesc" || return 1

        # Negative checks: invalid driver ID
        if "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=99U -c "${REPO_ROOT}/control_chassis/Core/UserDev/BladeMotor.c" -o "${WORK_DIR}/invalid.o" 2>/dev/null; then
            echo "Expected compilation failure for BLADE_MOTOR_DRIVER_USED=99U, but it succeeded!" >&2
            exit 1
        fi
        # Negative checks: ambiguous PWM definition
        if "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_PWM=7U -c "${REPO_ROOT}/control_chassis/Core/UserDev/BladeMotor.c" -o "${WORK_DIR}/ambiguous.o" 2>/dev/null; then
            echo "Expected compilation failure for BLADE_MOTOR_DRIVER_PWM=7U, but it succeeded!" >&2
            exit 1
        fi

        # USART2 SENS electrical/HIL selectors: both pull configurations and the
        # PD6 marker compile; invalid or unsafe combinations fail closed.
        printf '#ifndef MAIN_H\n#define MAIN_H\n#define __MAIN_H\n#define GPIO_MODE_AF_OD 0x12U\n#define GPIO_MODE_OUTPUT_PP 0x13U\n#define GPIO_NOPULL 0x34U\n#define GPIO_PULLUP 0x35U\n#define GPIO_SPEED_FREQ_LOW 0x56U\n#define GPIO_PIN_6 0x0040U\n#define GPIOD ((void *)0x40020C00U)\ntypedef struct UART_HandleTypeDef UART_HandleTypeDef;\n#endif\n' > "${WORK_DIR}/main.h"
        printf '#include "sens_ibus_usart2.h"\n#if SENS_IBUS_ELECTRICAL_OUTPUT_ENABLED != EXPECTED_SENS_ENABLED\n#error "Unexpected SENS output selector"\n#endif\n#if SENS_IBUS_INTERNAL_PULLUP_EXPERIMENT != EXPECTED_PULLUP_EXPERIMENT\n#error "Unexpected SENS pull-up experiment selector"\n#endif\n#if SENS_IBUS_HIL_SCOPE_MARKER_ENABLED != EXPECTED_SCOPE_MARKER\n#error "Unexpected SENS scope marker selector"\n#endif\n#if SENS_IBUS_GPIO_MODE != GPIO_MODE_AF_OD\n#error "SENS GPIO mode contract changed"\n#endif\n#if SENS_IBUS_GPIO_PULL != EXPECTED_GPIO_PULL\n#error "Unexpected SENS GPIO pull selection"\n#endif\n#if SENS_IBUS_GPIO_SPEED != GPIO_SPEED_FREQ_LOW\n#error "SENS GPIO speed contract changed"\n#endif\n#if SENS_IBUS_HIL_SCOPE_MARKER_PIN != GPIO_PIN_6\n#error "SENS scope marker moved from PD6"\n#endif\n#if SENS_IBUS_HIL_SCOPE_MARKER_MODE != GPIO_MODE_OUTPUT_PP\n#error "SENS scope marker mode changed"\n#endif\n#if SENS_IBUS_HIL_SCOPE_MARKER_PULL != GPIO_NOPULL\n#error "SENS scope marker pull changed"\n#endif\n#if SENS_IBUS_HIL_SCOPE_MARKER_SPEED != GPIO_SPEED_FREQ_LOW\n#error "SENS scope marker speed changed"\n#endif\nint main(void) { return 0; }\n' > "${WORK_DIR}/sens_ibus_config.c"
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DEXPECTED_SENS_ENABLED=0 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_disabled" || return 1
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DBLADE_HIL_UART_DEBUG_ENABLED=0 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=0 -DBLADE_MOTOR_DRIVER_USED=1U \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_nopull" || return 1
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DBLADE_HIL_UART_DEBUG_ENABLED=0 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=1 -DBLADE_MOTOR_DRIVER_USED=1U \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=1 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_PULLUP \
            "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_pullup" || return 1
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DBLADE_HIL_UART_DEBUG_ENABLED=0 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=0 -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=1 \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=1 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_scope_marker" || return 1
        printf '#include "sens_ibus_usart2.h"\n#if SENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH != EXPECTED_STATIC_HIGH\n#error "Unexpected SENS marker static-high selector"\n#endif\nint main(void) { return 0; }\n' > "${WORK_DIR}/sens_ibus_static_high.c"
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DBLADE_HIL_UART_DEBUG_ENABLED=0 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=1 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH=1 -DEXPECTED_STATIC_HIGH=1 \
            "${WORK_DIR}/sens_ibus_static_high.c" -o "${WORK_DIR}/sens_static_high" || return 1
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=0 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH=1 -DEXPECTED_STATIC_HIGH=1 \
            -c "${WORK_DIR}/sens_ibus_static_high.c" -o "${WORK_DIR}/sens_static_without_marker.o" 2>/dev/null; then
            echo "Expected static-high hold without scope marker to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=1 \
            -DSENS_IBUS_HIL_SCOPE_MARKER_STATIC_HIGH=2 -DEXPECTED_STATIC_HIGH=2 \
            -c "${WORK_DIR}/sens_ibus_static_high.c" -o "${WORK_DIR}/sens_invalid_static.o" 2>/dev/null; then
            echo "Expected invalid static-high selector to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        printf '#include "sens_ibus_usart2.h"\n#if SENS_IBUS_HIL_DIAG_USART1_TX_ENABLED != EXPECTED_DIAG\n#error "Unexpected USART1 diagnostic selector"\n#endif\nint main(void) { return 0; }\n' > "${WORK_DIR}/sens_ibus_diag.c"
        "$cc" -std=c99 -Wall -Wextra -Werror -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DBLADE_HIL_UART_DEBUG_ENABLED=0 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_HIL_DIAG_USART1_TX_ENABLED=1 -DEXPECTED_DIAG=1 \
            "${WORK_DIR}/sens_ibus_diag.c" -o "${WORK_DIR}/sens_diag" || return 1
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=0 \
            -DSENS_IBUS_HIL_DIAG_USART1_TX_ENABLED=1 -DEXPECTED_DIAG=1 \
            -c "${WORK_DIR}/sens_ibus_diag.c" -o "${WORK_DIR}/sens_diag_without_output.o" 2>/dev/null; then
            echo "Expected USART1 diagnostics without SENS output to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DSENS_IBUS_HIL_DIAG_USART1_TX_ENABLED=2 -DEXPECTED_DIAG=2 \
            -c "${WORK_DIR}/sens_ibus_diag.c" -o "${WORK_DIR}/sens_invalid_diag.o" 2>/dev/null; then
            echo "Expected invalid USART1 diagnostic selector to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DBLADE_HIL_UART_DEBUG_ENABLED=1 -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            -c "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_debug_conflict.o" 2>/dev/null; then
            echo "Expected USART2 HIL debug/SENS conflict to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 -DSENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=2 \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=2 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            -c "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_invalid_pullup.o" 2>/dev/null; then
            echo "Expected invalid SENS pull-up experiment selector to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=0 -DSENS_IBUS_INTERNAL_PULLUP_EXPERIMENT=1 \
            -DEXPECTED_SENS_ENABLED=0 -DEXPECTED_PULLUP_EXPERIMENT=1 \
            -DEXPECTED_SCOPE_MARKER=0 -DEXPECTED_GPIO_PULL=GPIO_PULLUP \
            -c "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_pullup_without_output.o" 2>/dev/null; then
            echo "Expected pull-up experiment without SENS output to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=1 -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=2 \
            -DEXPECTED_SENS_ENABLED=1 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=2 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            -c "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_invalid_marker.o" 2>/dev/null; then
            echo "Expected invalid SENS scope marker selector to fail compilation, but it succeeded!" >&2
            exit 1
        fi
        if "$cc" -std=c99 -I"${WORK_DIR}" -include "${WORK_DIR}/main.h" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -DSENS_IBUS_ELECTRICAL_OUTPUT_ENABLED=0 -DSENS_IBUS_HIL_SCOPE_MARKER_ENABLED=1 \
            -DEXPECTED_SENS_ENABLED=0 -DEXPECTED_PULLUP_EXPERIMENT=0 \
            -DEXPECTED_SCOPE_MARKER=1 -DEXPECTED_GPIO_PULL=GPIO_NOPULL \
            -c "${WORK_DIR}/sens_ibus_config.c" -o "${WORK_DIR}/sens_marker_without_output.o" 2>/dev/null; then
            echo "Expected scope marker without SENS output to fail compilation, but it succeeded!" >&2
            exit 1
        fi
    } >"$log" 2>&1 || {
        fail "Blade Driver Selection Harness"
        cat "$log" >&2
        return 1
    }
    pass "Blade Driver Selection (default FTESC CAN, explicit PWM/FTESC CAN, negative compile guards)"

    info "=== [2/9] Running Blade Manual Control Harness ==="
    log="${LOG_DIR}/host_blade_manual_control.log"
    local bmc_common=(-std=c99 -Wall -Wextra -Werror -I"${REPO_ROOT}/control_chassis/host/fakes" -I"${REPO_ROOT}/control_chassis/Core/Inc" -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1)
    local bmc_sources=(
        "${REPO_ROOT}/control_chassis/host/blade_manual_control_harness.c"
        "${REPO_ROOT}/control_chassis/Core/UserApp/blade_manual_control.c"
        "${REPO_ROOT}/control_chassis/Core/UserApp/safety_ownership_gate.c"
    )
    {
        "$cc" "${bmc_common[@]}" -DBLADE_MOTOR_DRIVER_USED=1U "${bmc_sources[@]}" -lm -o "${WORK_DIR}/bmc_pwm" || return 1
        "${WORK_DIR}/bmc_pwm" || return 1
        "$cc" "${bmc_common[@]}" -DBLADE_MOTOR_DRIVER_USED=2U "${bmc_sources[@]}" -lm -o "${WORK_DIR}/bmc_ftesc" || return 1
        "${WORK_DIR}/bmc_ftesc" || return 1
    } >"$log" 2>&1 || {
        fail "Blade Manual Control Harness"
        cat "$log" >&2
        return 1
    }
    pass "Blade Manual Control (PWM & FTESC CAN confirmation state machine)"

    info "=== [3/9] Running Blade Confirm CH6 Adapter Harness ==="
    log="${LOG_DIR}/host_blade_confirm_ch6_adapter.log"
    local ch6_common=(-std=c99 -Wall -Wextra -Werror -I"${REPO_ROOT}/control_chassis/host/fakes" -I"${REPO_ROOT}/control_chassis/Core/Inc" -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1)
    local ch6_sources=(
        "${REPO_ROOT}/control_chassis/host/blade_confirm_ch6_adapter_harness.c"
        "${REPO_ROOT}/control_chassis/Core/UserDev/blade_confirm_ch6_adapter.c"
        "${REPO_ROOT}/control_chassis/Core/UserApp/blade_manual_control.c"
        "${REPO_ROOT}/control_chassis/Core/UserApp/safety_ownership_gate.c"
    )
    {
        "$cc" "${ch6_common[@]}" "${ch6_sources[@]}" -lm -o "${WORK_DIR}/ch6_adapter"
        "${WORK_DIR}/ch6_adapter"
    } >"$log" 2>&1 || {
        fail "Blade Confirm CH6 Adapter Harness"
        cat "$log" >&2
        return 1
    }
    pass "Blade Confirm CH6 Adapter (Baseline, edge debounce, duplicate rejection & BMC integration)"

    info "=== Running CAN TX scheduler failure/interleaving harness (-O0/-O2) ==="
    log="${LOG_DIR}/host_can_tx_scheduler.log"
    python3 "${REPO_ROOT}/tools/test_can_tx_scheduler.py" >"$log" 2>&1 || {
        fail "CAN TX scheduler"
        cat "$log" >&2
        return 1
    }
    pass "CAN TX scheduler (stop retention, completion and mailbox ordering)"

    info "=== [4/9] Running FTESC Realtime Feedback Harness ==="
    log="${LOG_DIR}/host_ftesc_feedback.log"
    {
        "$cc" -std=c99 -Wall -Wextra -Werror -DFLIPSKY_HOST_TEST \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/ftesc_realtime_feedback_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/flipsky_can.c" \
            "${REPO_ROOT}/control_chassis/Core/UserApp/telemetry_snapshot.c" \
            -lm -o "${WORK_DIR}/ftesc_feedback"
        "${WORK_DIR}/ftesc_feedback"
    } >"$log" 2>&1 || {
        fail "FTESC Realtime Feedback Harness"
        cat "$log" >&2
        return 1
    }
    pass "FTESC Realtime Feedback (CAN packet parsing & CRC validation)"

    info "=== [5/9] Running SENS i-BUS Responder Harness ==="
    log="${LOG_DIR}/host_sens_ibus_responder.log"
    {
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/sens_ibus_responder_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/sens_ibus_responder.c" \
            -lm -o "${WORK_DIR}/sens_ibus_responder"
        "${WORK_DIR}/sens_ibus_responder"
    } >"$log" 2>&1 || {
        fail "SENS i-BUS Responder Harness"
        cat "$log" >&2
        return 1
    }
    pass "SENS i-BUS Responder (Discovery, measurement request & packet packing)"

    info "=== [6/9] Running Telemetry Snapshot Harness ==="
    log="${LOG_DIR}/host_telemetry_snapshot.log"
    {
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/telemetry_snapshot_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserApp/telemetry_snapshot.c" \
            -lm -o "${WORK_DIR}/telemetry_snapshot"
        "${WORK_DIR}/telemetry_snapshot"
    } >"$log" 2>&1 || {
        fail "Telemetry Snapshot Harness"
        cat "$log" >&2
        return 1
    }
    pass "Telemetry Snapshot (BMS, odometry, fault bitfield & atomic exchange)"

    info "=== [7/9] Running Remote Authorization Harness ==="
    log="${LOG_DIR}/host_remote_authorization.log"
    {
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/remote_authorization_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/remote_authorization.c" \
            -lm -o "${WORK_DIR}/remote_authorization"
        "${WORK_DIR}/remote_authorization"
    } >"$log" 2>&1 || {
        fail "Remote Authorization Harness"
        cat "$log" >&2
        return 1
    }
    pass "Remote Authorization (CH8 dual-edge unlock state machine)"

    info "=== Running i-BUS Snapshot Concurrency Harness ==="
    python3 "${REPO_ROOT}/tools/test_remote_rx_queue.py" >"${LOG_DIR}/host_remote_rx_queue.log" 2>&1 || {
        fail "Remote timestamped UART queue regression"
        cat "${LOG_DIR}/host_remote_rx_queue.log" >&2
        return 1
    }
    pass "Remote receive timestamps, stale queue rejection and stream continuity"

    python3 "${REPO_ROOT}/tools/test_ibus_snapshot.py" >"${LOG_DIR}/host_ibus_snapshot.log" 2>&1 || {
        fail "i-BUS Snapshot Concurrency Harness"
        cat "${LOG_DIR}/host_ibus_snapshot.log" >&2
        return 1
    }
    pass "i-BUS Snapshot (single writer, preemption, independent timeout, IRQ restore; O0/O2)"

    info "=== [8/9] Running Remote Backend Harness ==="
    log="${LOG_DIR}/host_remote_backend.log"
    {
        # A28/E34 Legacy Backend Test
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -DREMOTE_BACKEND_HOST \
            -DREMOTE_BACKEND_A28_E34=1 -DREMOTE_BACKEND_FLYSKY_IBUS=0 \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/remote_backend_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/remote_backend.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/remote_authorization.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/flysky_ibus_servo.c" \
            -lm -o "${WORK_DIR}/remote_backend_a28" || return 1
        "${WORK_DIR}/remote_backend_a28" || return 1

        local timeout_ms
        local timeout_defs=()
        for timeout_ms in default 100 300; do
            timeout_defs=()
            if [[ "$timeout_ms" != default ]]; then
                timeout_defs=(-DREMOTE_IBUS_TIMEOUT_MS="${timeout_ms}U")
            fi
            # FlySky i-BUS Backend Test
            "$cc" -std=c99 -Wall -Wextra -Werror \
                -DREMOTE_BACKEND_HOST "${timeout_defs[@]}" \
                -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
                -I"${REPO_ROOT}/control_chassis/Core/Inc" \
                "${REPO_ROOT}/control_chassis/host/remote_backend_harness.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/remote_backend.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/remote_authorization.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/flysky_ibus_servo.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/blade_confirm_ch6_adapter.c" \
                -lm -o "${WORK_DIR}/remote_backend_flysky_${timeout_ms}" || return 1
            "${WORK_DIR}/remote_backend_flysky_${timeout_ms}" || return 1
            # Exercise the production CH6 bag + explicit OK blade configuration.
            "$cc" -std=c99 -Wall -Wextra -Werror \
                -DREMOTE_BACKEND_HOST "${timeout_defs[@]}" -DMOWER_BLADE_OK_ENABLED=1 -DMOWER_GRASS_BAG_CH6_ENABLED=1 \
                -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
                -I"${REPO_ROOT}/control_chassis/Core/Inc" \
                "${REPO_ROOT}/control_chassis/host/remote_backend_harness.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/remote_backend.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/remote_authorization.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/flysky_ibus_servo.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/blade_confirm_ch6_adapter.c" \
                -lm -o "${WORK_DIR}/remote_backend_bag_release_${timeout_ms}" || return 1
            "${WORK_DIR}/remote_backend_bag_release_${timeout_ms}" || return 1
        done
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/grass_bag_control_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
            -o "${WORK_DIR}/grass_bag_control" || return 1
        "${WORK_DIR}/grass_bag_control" || return 1
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -DMOWER_GRASS_BAG_CH6_ENABLED=1 -DMOWER_BLADE_OK_ENABLED=1 \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -include "${REPO_ROOT}/control_chassis/host/lifter_fakes/main.h" \
            -I"${REPO_ROOT}/control_chassis/host/lifter_fakes" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/grass_bag_lifter_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/lifter.c" \
            "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
            -lm -o "${WORK_DIR}/grass_bag_lifter" || return 1
        "${WORK_DIR}/grass_bag_lifter" || return 1
        printf '#include "remote_backend_config.h"\n' > "${WORK_DIR}/bag_conflict.c"
        if "$cc" -std=c99 -DMOWER_GRASS_BAG_CH6_ENABLED=1 -DMOWER_BLADE_OK_ENABLED=0 \
            -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            -c "${WORK_DIR}/bag_conflict.c" -o "${WORK_DIR}/bag_conflict.o" 2>/dev/null; then
            echo "Expected CH6 bag/legacy blade confirmation conflict to fail compilation" >&2
            exit 1
        fi
        for timeout_ms in 0 200 301; do
            if "$cc" -std=c99 -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
                -DREMOTE_IBUS_TIMEOUT_MS="${timeout_ms}U" -I"${REPO_ROOT}/control_chassis/Core/Inc" \
                -c "${WORK_DIR}/bag_conflict.c" -o "${WORK_DIR}/invalid_timeout.o" 2>/dev/null; then
                echo "Expected unsupported i-BUS timeout ${timeout_ms}ms to fail compilation" >&2
                return 1
            fi
        done
    } >"$log" 2>&1 || {
        fail "Remote Backend Harness"
        cat "$log" >&2
        return 1
    }
    pass "Remote Backend (A28/E34 & FlySky i-BUS stream/framing isolation)"

    python3 "${REPO_ROOT}/tools/test_actuator_pwm.py" >"${LOG_DIR}/host_actuator_pwm.log" 2>&1 || {
        fail "Actuator PWM clock, timer and H-bridge regression"
        cat "${LOG_DIR}/host_actuator_pwm.log" >&2
        return 1
    }
    pass "Actuator PWM (actual RCC/TIM/EG2104, 10kHz, +/-60%, startup and stop zero)"

    info "=== Running Lifter Height Homing Harness (both remote backends) ==="
    log="${LOG_DIR}/host_lifter_height.log"
    {
        for backend in a28 flysky; do
            local height_defs=(-DREMOTE_BACKEND_A28_E34=1 -DREMOTE_BACKEND_FLYSKY_IBUS=0)
            if [[ "$backend" == flysky ]]; then
                height_defs=(-DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1
                    -DMOWER_GRASS_BAG_CH6_ENABLED=1 -DMOWER_BLADE_OK_ENABLED=1)
            fi
            "$cc" -std=c99 -Wall -Wextra -Werror "${height_defs[@]}" \
                -include "${REPO_ROOT}/control_chassis/host/lifter_fakes/main.h" \
                -I"${REPO_ROOT}/control_chassis/host/lifter_fakes" \
                -I"${REPO_ROOT}/control_chassis/Core/Inc" \
                "${REPO_ROOT}/control_chassis/host/lifter_height_harness.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/lifter.c" \
                "${REPO_ROOT}/control_chassis/Core/UserDev/grass_bag_control.c" \
                -lm -o "${WORK_DIR}/lifter_height_${backend}" || return 1
            "${WORK_DIR}/lifter_height_${backend}" || return 1
        done
    } >"$log" 2>&1 || {
        fail "Lifter Height Homing Harness"
        cat "$log" >&2
        return 1
    }
    pass "Lifter Height (interruptible 10s homing, reversed PWM, latest target wins)"

    python3 "${REPO_ROOT}/tools/check_i6x_calibration.py" >"${LOG_DIR}/host_i6x_calibration.log" 2>&1 || {
        cat "${LOG_DIR}/host_i6x_calibration.log" >&2
        return 1
    }
    pass "i6X stick calibration validation and neutral fallback"

    python3 "${REPO_ROOT}/tools/check_i6x_startup_warning.py" >"${LOG_DIR}/host_i6x_startup_warning.log" 2>&1 || {
        cat "${LOG_DIR}/host_i6x_startup_warning.log" >&2
        return 1
    }
    pass "i6X mower direct startup, native throttle/switch warnings and calibration isolation"


    info "=== Running IMU Fault / Runtime / SPI Deadline Harnesses ==="
    for imu_test in imu_fault imu_runtime imu_spi_deadline; do
        python3 "${REPO_ROOT}/tools/test_${imu_test}.py" >"${LOG_DIR}/host_${imu_test}.log" 2>&1 || {
            cat "${LOG_DIR}/host_${imu_test}.log" >&2
            return 1
        }
    done
    pass "IMU fault propagation, runtime diagnostics and bounded HAL deadlines"

    info "=== Running IMU Numeric Harness (optimized strict aliasing) ==="
    local imu_log="${LOG_DIR}/host_imu_solver.log"
    printf '#ifndef __MAIN_H\n#define __MAIN_H\n#include <stdint.h>\n#endif\n' > "${WORK_DIR}/imu_main.h"
    {
        "$cc" -std=c99 -O2 -fstrict-aliasing -Wall -Wextra -Werror \
            -Wno-missing-field-initializers -Wstrict-aliasing=2 \
            -fsanitize=address,undefined -include "${WORK_DIR}/imu_main.h" \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/imu_solver_harness.c" -lm \
            -o "${WORK_DIR}/imu_solver" || return 1
        ASAN_OPTIONS=detect_leaks=0 "${WORK_DIR}/imu_solver" || return 1
    } >"$imu_log" 2>&1 || {
        fail "IMU Numeric harness failed"
        cat "$imu_log" >&2
        return 1
    }
    pass "IMU Numeric (accuracy, pose and strict-aliasing optimization)"

    info "=== [9/9] Running Safety Ownership Gate Harness ==="
    log="${LOG_DIR}/host_safety_ownership_gate.log"
    {
        "$cc" -std=c99 -Wall -Wextra -Werror \
            -I"${REPO_ROOT}/control_chassis/Core/Inc" \
            "${REPO_ROOT}/control_chassis/host/safety_ownership_gate_harness.c" \
            "${REPO_ROOT}/control_chassis/Core/UserApp/safety_ownership_gate.c" \
            -lm -o "${WORK_DIR}/safety_ownership_gate"
        "${WORK_DIR}/safety_ownership_gate"
    } >"$log" 2>&1 || {
        fail "Safety Ownership Gate Harness"
        cat "$log" >&2
        return 1
    }
    pass "Safety Ownership Gate (Arbitration, latching, zero-intent enforcement)"

    echo ""
    "${REPO_ROOT}/experiments/ticket03/run_host.sh" >"${LOG_DIR}/host_exp3.log" 2>&1 || {
        cat "${LOG_DIR}/host_exp3.log" >&2
        return 1
    }
    pass "Ticket03 Experiment3 protocol and transport"
    "${REPO_ROOT}/tools/verify_blade_ok_host.sh" >"${LOG_DIR}/host_blade_ok.log" 2>&1 || {
        cat "${LOG_DIR}/host_blade_ok.log" >&2
        return 1
    }
    pass "Mower OK release flow, controller runtime and home display"
    python3 "${REPO_ROOT}/tools/test_api_ownership.py" >"${LOG_DIR}/host_api_ownership.log" 2>&1 || {
        fail "Production API ownership call-site regression failed"
        cat "${LOG_DIR}/host_api_ownership.log" >&2
        return 1
    }
    pass "Production API ownership and safety boundaries"
    echo -e "${GREEN}${BOLD}ALL HOST HARNESSES PASSED!${NC}"
    return 0
}

run_chassis_build() {
    info "=== Building Chassis Firmware (CMake / ARM GCC) ==="
    local log="${LOG_DIR}/chassis_build.log"
    local chassis_dir="${REPO_ROOT}/control_chassis"

    if ! source "${REPO_ROOT}/tools/gcc15_env.sh"; then
        fail "Pinned GCC 15.3 toolchain is unavailable"
        return 1
    fi

    if ! command -v cmake >/dev/null 2>&1; then
        fail "cmake not found in PATH"
        return 1
    fi

    if ! command -v ninja >/dev/null 2>&1; then
        fail "ninja not found in PATH"
        return 1
    fi

    {
        echo "Using CMake: $(cmake --version | head -n1)"
        echo "Using Toolchain: $(which arm-none-eabi-gcc)"
        (
            cd "$chassis_dir"
            cmake --preset debug || exit 1
            cmake --build --preset debug || exit 1
        )
    } >"$log" 2>&1 || {
        fail "Chassis build failed"
        cat "$log" >&2
        return 1
    }

    if [[ ! -f "${chassis_dir}/build/gcc15/debug/control_chassis.elf" || ! -f "${chassis_dir}/build/gcc15/debug/control_chassis.hex" || ! -f "${chassis_dir}/build/gcc15/debug/control_chassis.bin" ]]; then
        fail "Build finished without creating target binary"
        cat "$log" >&2
        return 1
    fi

    pass "Chassis CMake build succeeded (ELF, HEX and BIN generated)"
    return 0
}

usage() {
    echo "Usage: $0 {host|chassis|all}"
    echo ""
    echo "  host     Run all deterministic C host harnesses (9 test suites)"
    echo "  chassis  Build the control_chassis ARM Cortex-M4 firmware with CMake/ARM GCC"
    echo "  all      Run host harnesses followed by chassis firmware build"
    exit 1
}

main() {
    local target="${1:-all}"
    case "$target" in
        host)
            run_host_harnesses
            ;;
        chassis)
            run_chassis_build
            ;;
        all)
            run_host_harnesses
            echo ""
            run_chassis_build
            ;;
        *)
            usage
            ;;
    esac
}

main "$@"
