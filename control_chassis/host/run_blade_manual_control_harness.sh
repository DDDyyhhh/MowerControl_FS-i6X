#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-gcc}
common=(-std=c99 -Wall -Wextra -Werror -I"$root_dir/host/fakes" -I"$root_dir/Core/Inc" -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1)
sources=(
    "$root_dir/host/blade_manual_control_harness.c"
    "$root_dir/Core/UserApp/blade_manual_control.c"
    "$root_dir/Core/UserApp/safety_ownership_gate.c"
)

"$cc" "${common[@]}" "${sources[@]}" -lm -o /tmp/blade_manual_control_pwm_harness
/tmp/blade_manual_control_pwm_harness

"$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=2U \
    "${sources[@]}" -lm -o /tmp/blade_manual_control_ftesc_harness
/tmp/blade_manual_control_ftesc_harness

echo "blade manual control harness passed"
