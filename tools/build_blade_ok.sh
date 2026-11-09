#!/usr/bin/env bash
# Builds the accepted mower release firmware. Does not program either board.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/tools/gcc15_env.sh"
"$root/tools/verify.sh" all
cmake --preset mower-ok-release -S "$root/control_chassis"
cmake --build "$root/control_chassis/build/gcc15/mower-ok-release"
source_dir="$root/OpenI6X/FS-I6X/opentx"
radio_build_dir="${MOWER_RADIO_BUILD_DIR:-$source_dir/build-mower-ok-gcc15}"
cmake -S "$source_dir" -B "$radio_build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/i6x-toolchain-linux.cmake" \
  -DARM_GNU_TOOLCHAIN_ROOT="$ARM_GNU_TOOLCHAIN_ROOT" \
  -DPCB=I6X -DTRANSLATIONS=EN -DPCBI6X_MOWER_UI_SHELL=YES \
  -DPCBI6X_MOWER_UI_MOCK=NO -DPCBI6X_TICKET03_HIL_MODEL=NO \
  -DPCBI6X_MOWER_BLADE_OK=ON -DPCBI6X_EXP3=OFF -DPCBI6X_EXP3_PWM_HIL=OFF
cmake --build "$radio_build_dir" --target firmware -j8
python3 "$root/tools/test_i6x_sensor_arm.py" "$radio_build_dir/firmware.elf" "$radio_build_dir/firmware.bin"
arm-none-eabi-size "$radio_build_dir/firmware.elf" \
  "$root/control_chassis/build/gcc15/mower-ok-release/control_chassis.elf"
sha256sum "$radio_build_dir/firmware.bin" \
  "$root/control_chassis/build/gcc15/mower-ok-release/control_chassis.bin" \
  "$root/control_chassis/build/gcc15/mower-ok-release/control_chassis.hex"
