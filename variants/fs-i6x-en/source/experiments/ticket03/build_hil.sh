#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
"$root/experiments/ticket03/run_host.sh"
cmake --preset exp3-hil -S "$root/control_chassis"
cmake --build "$root/control_chassis/build/exp3-hil"
cmake --preset exp3-pwm-hil -S "$root/control_chassis"
cmake --build "$root/control_chassis/build/exp3-pwm-hil"
source_dir="$root/OpenI6X/FS-I6X/opentx"
cmake -S "$source_dir" -B "$source_dir/build-exp3-hil" \
    -DCMAKE_TOOLCHAIN_FILE="$source_dir/i6x-toolchain-linux.cmake" \
    -DPCB=I6X -DTRANSLATIONS=EN -DPCBI6X_SENS_HIL_DIAG=YES \
    -DPCBI6X_MOWER_UI_SHELL=YES -DPCBI6X_MOWER_UI_MOCK=NO \
    -DPCBI6X_TICKET03_HIL_MODEL=NO -DPCBI6X_EXP3=ON
cmake --build "$source_dir/build-exp3-hil" --target firmware -j8
cmake -S "$source_dir" -B "$source_dir/build-exp3-pwm-hil" \
    -DCMAKE_TOOLCHAIN_FILE="$source_dir/i6x-toolchain-linux.cmake" \
    -DPCB=I6X -DTRANSLATIONS=EN -DPCBI6X_SENS_HIL_DIAG=YES \
    -DPCBI6X_MOWER_UI_SHELL=YES -DPCBI6X_MOWER_UI_MOCK=NO \
    -DPCBI6X_TICKET03_HIL_MODEL=NO -DPCBI6X_EXP3=ON \
    -DPCBI6X_EXP3_PWM_HIL=ON
cmake --build "$source_dir/build-exp3-pwm-hil" --target firmware -j8
arm-none-eabi-size "$source_dir/build-exp3-hil/firmware.elf" \
    "$source_dir/build-exp3-pwm-hil/firmware.elf" \
    "$root/control_chassis/build/exp3-hil/control_chassis.elf" \
    "$root/control_chassis/build/exp3-pwm-hil/control_chassis.elf"
sha256sum "$source_dir/build-exp3-hil/firmware.bin" \
    "$source_dir/build-exp3-pwm-hil/firmware.bin" \
    "$root/control_chassis/build/exp3-hil/control_chassis.hex" \
    "$root/control_chassis/build/exp3-pwm-hil/control_chassis.hex"
