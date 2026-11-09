#!/usr/bin/env bash
# Remote-only production build. Never builds or programs the chassis.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$root/OpenI6X/FS-I6X/opentx"
build_dir="$source_dir/build-mower-user-ui"
python_bin="${MOWER_UI_PYTHON:-python3}"
"$python_bin" "$root/tools/test_blade_ok_ui.py"
cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/i6x-toolchain-linux.cmake" \
  -DPython3_EXECUTABLE="$(command -v "$python_bin")" -DDISABLE_COMPANION=ON \
  -DPCB=I6X -DTRANSLATIONS=EN -DPCBI6X_MOWER_UI_SHELL=YES \
  -DPCBI6X_MOWER_UI_MOCK=NO -DPCBI6X_TICKET03_HIL_MODEL=NO \
  -DPCBI6X_MOWER_BLADE_OK=ON -DPCBI6X_EXP3=OFF -DPCBI6X_EXP3_PWM_HIL=OFF
cmake --build "$build_dir" --target firmware -j8
arm-none-eabi-size "$build_dir/firmware.elf"
shasum -a 256 "$build_dir/firmware.bin"
