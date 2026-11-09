#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d /tmp/blade-ok-host.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat > "$work/main.h" <<'FAKE'
#include <stdint.h>
uint32_t HAL_GetTick(void);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
struct FakeSysTick { uint32_t VAL; };
extern struct FakeSysTick fake_systick;
#define SysTick (&fake_systick)
FAKE
p="$root/experiments/ticket03"
flags=(-std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address
  -DEXP3_ENABLED=1 -DEXP3_PWM_PROBE_ENABLED=1 -DEXP3_AUTO_SESSION_ENABLED=1
  -DMOWER_BLADE_OK_ENABLED=1 -I"$work" -I"$p")
core=("$p/exp3.c" "$p/exp3_link.c" "$p/exp3_hil.c" "$p/exp3_auto_session.c")
cc "${flags[@]}" -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 -I"$root/control_chassis/Core/Inc" "$p/test_release_board.c" "${core[@]}" "$p/exp3_probe.c" "$p/exp3_blade_adapter.c" "$p/stm32_adapter.c" "$root/control_chassis/Core/UserApp/blade_manual_control.c" "$root/control_chassis/Core/UserDev/mower_mode_control.c" -o "$work/board"
ASAN_OPTIONS=detect_leaks=0 "$work/board"
cc "${flags[@]}" "$p/test_release_radio.c" "${core[@]}" "$p/openi6x_adapter.c" -o "$work/radio"
ASAN_OPTIONS=detect_leaks=0 "$work/radio"

cc -std=c99 -Wall -Wextra -Werror -DREMOTE_BACKEND_HOST -DMOWER_BLADE_OK_ENABLED=1 \
  -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
  -I"$root/control_chassis/Core/Inc" \
  "$root/control_chassis/host/mower_backend_harness.c" \
  "$root/control_chassis/Core/UserDev/remote_backend.c" \
  "$root/control_chassis/Core/UserDev/mower_mode_control.c" \
  "$root/control_chassis/Core/UserDev/remote_authorization.c" \
  "$root/control_chassis/Core/UserDev/flysky_ibus_servo.c" \
  "$root/control_chassis/Core/UserDev/blade_confirm_ch6_adapter.c" -lm -o "$work/backend"
"$work/backend"
echo 'PASS production backend CH6 cannot create confirmation; CH7 mapping preserved'

cc "${flags[@]}" -I"$root/control_chassis/Core/Inc" "$p/test_release_sens.c" \
  "${core[@]}" "$p/exp3_probe.c" "$p/stm32_adapter.c" \
  "$root/control_chassis/Core/UserDev/sens_ibus_responder.c" -o "$work/sens"
ASAN_OPTIONS=detect_leaks=0 "$work/sens"
cc "${flags[@]}" -DMOWER_GRASS_BAG_CH6_ENABLED=1 \
  -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
  -I"$root/control_chassis/Core/Inc" "$p/test_release_sens.c" \
  "${core[@]}" "$p/exp3_probe.c" "$p/stm32_adapter.c" \
  "$root/control_chassis/Core/UserDev/sens_ibus_responder.c" -o "$work/bag-sens"
ASAN_OPTIONS=detect_leaks=0 "$work/bag-sens"
python3 "$root/tools/test_blade_ok_ui.py"
python3 "$root/tools/test_bag_connection_source.py"

cc "${flags[@]}" "$p/test_vectors.c" "$p/exp3.c" -o "$work/vectors"
ASAN_OPTIONS=detect_leaks=0 python3 "$p/check_vectors.py" "$work/vectors" 0x33

cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
  -I"$root/control_chassis/Core/Inc" "$root/control_chassis/host/mower_mode_control_harness.c" \
  "$root/control_chassis/Core/UserDev/mower_mode_control.c" -o "$work/mode"
ASAN_OPTIONS=detect_leaks=0 "$work/mode"
python3 "$root/tools/test_ch8_host_commands.py"
python3 "$root/tools/test_ch8_lifter_permission.py"
python3 "$root/tools/test_ch8_motion_gate.py"
