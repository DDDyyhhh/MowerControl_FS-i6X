#!/usr/bin/env bash
# Builds the accepted mower release firmware. Does not program either board.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
"$root/tools/verify.sh" all
cmake --preset mower-ok-release -S "$root/control_chassis"
cmake --build "$root/control_chassis/build/mower-ok-release"
source_dir="$root/OpenI6X/FS-I6X/opentx"
cmake -S "$source_dir" -B "$source_dir/build-mower-ok-release" \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/i6x-toolchain-linux.cmake" \
  -DPCB=I6X -DTRANSLATIONS=EN -DPCBI6X_MOWER_UI_SHELL=YES \
  -DPCBI6X_MOWER_UI_MOCK=NO -DPCBI6X_TICKET03_HIL_MODEL=NO \
  -DPCBI6X_MOWER_BLADE_OK=ON -DPCBI6X_EXP3=OFF -DPCBI6X_EXP3_PWM_HIL=OFF
cmake --build "$source_dir/build-mower-ok-release" --target firmware -j8
arm-none-eabi-size "$source_dir/build-mower-ok-release/firmware.elf" \
  "$root/control_chassis/build/mower-ok-release/control_chassis.elf"
sha256sum "$source_dir/build-mower-ok-release/firmware.bin" \
  "$root/control_chassis/build/mower-ok-release/control_chassis.bin" \
  "$root/control_chassis/build/mower-ok-release/control_chassis.hex"
