#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-gcc}
common=(-std=c99 -Wall -Wextra -Werror -I"$root_dir/host/fakes" -I"$root_dir/Core/Inc")
sources=(
    "$root_dir/host/blade_driver_selection_harness.c"
    "$root_dir/Core/UserDev/BladeMotor.c"
    "$root_dir/Core/UserApp/safety_ownership_gate.c"
)

"$cc" "${common[@]}" -DEXPECTED_BLADE_DRIVER=2U \
    "${sources[@]}" -lm -o /tmp/blade_driver_default_harness
/tmp/blade_driver_default_harness

"$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=1U -DEXPECTED_BLADE_DRIVER=1U \
    "${sources[@]}" -lm -o /tmp/blade_driver_pwm_harness
/tmp/blade_driver_pwm_harness

"$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=2U -DEXPECTED_BLADE_DRIVER=2U \
    "${sources[@]}" -lm -o /tmp/blade_driver_ftesc_harness
/tmp/blade_driver_ftesc_harness

if "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_USED=99U \
    -c "$root_dir/Core/UserDev/BladeMotor.c" -o /tmp/blade_driver_invalid.o \
    >/tmp/blade_driver_invalid.log 2>&1; then
    echo "invalid blade driver selection compiled unexpectedly" >&2
    exit 1
fi

if "$cc" "${common[@]}" -DBLADE_MOTOR_DRIVER_PWM=7U \
    -c "$root_dir/Core/UserDev/BladeMotor.c" -o /tmp/blade_driver_ambiguous.o \
    >/tmp/blade_driver_ambiguous.log 2>&1; then
    echo "ambiguous blade driver configuration compiled unexpectedly" >&2
    exit 1
fi

echo "blade driver compile-time checks passed"
