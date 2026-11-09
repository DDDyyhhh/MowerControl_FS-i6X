#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d /tmp/exp3-host.XXXXXX)
trap 'rm -rf "$work"' EXIT
cc=${CC:-cc}
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  "$root/test_exp3.c" "$root/exp3.c" "$root/exp3_link.c" "$root/exp3_hil.c" -o "$work/test"
ASAN_OPTIONS=detect_leaks=0 "$work/test"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  "$root/test_probe.c" "$root/exp3.c" "$root/exp3_probe.c" -o "$work/probe"
ASAN_OPTIONS=detect_leaks=0 "$work/probe"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  "$root/test_fast_result.c" "$root/exp3.c" "$root/exp3_link.c" "$root/exp3_hil.c" \
  -o "$work/fast-result"
ASAN_OPTIONS=detect_leaks=0 "$work/fast-result"
"$cc" -std=c99 -Wall -Wextra -Werror -DEXP3_ENABLED=1 \
  "$root/test_ui_event.c" -o "$work/ui-event"
"$work/ui-event"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  "$root/test_auto_session.c" "$root/exp3.c" "$root/exp3_link.c" "$root/exp3_hil.c" \
  "$root/exp3_auto_session.c" -o "$work/auto-session"
ASAN_OPTIONS=detect_leaks=0 "$work/auto-session"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
  -I"$root" -I"$root/../../control_chassis/Core/Inc" \
  "$root/test_blade_adapter.c" "$root/exp3.c" "$root/exp3_probe.c" \
  "$root/exp3_blade_adapter.c" \
  "$root/../../control_chassis/Core/UserApp/blade_manual_control.c" -o "$work/blade"
ASAN_OPTIONS=detect_leaks=0 "$work/blade"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address \
  -DEXP3_ENABLED=1 -DREMOTE_BACKEND_A28_E34=0 -DREMOTE_BACKEND_FLYSKY_IBUS=1 \
  -I"$root" -I"$root/../../control_chassis/Core/Inc" \
  "$root/test_servo_transport_gate.c" \
  "$root/../../control_chassis/Core/UserDev/flysky_ibus_servo.c" -o "$work/servo-gate"
ASAN_OPTIONS=detect_leaks=0 "$work/servo-gate"
"$cc" -std=c99 -Wall -Wextra -Werror -c "$root/exp3.c" -o "$work/disabled.o"
test -z "$(nm -g --defined-only "$work/disabled.o")"
"$cc" -std=c99 -Wall -Wextra -Werror -c "$root/exp3_probe.c" -o "$work/probe-disabled.o"
"$cc" -std=c99 -Wall -Wextra -Werror -I"$root" \
  -c "$root/exp3_blade_adapter.c" -o "$work/blade-disabled.o"
test -z "$(nm -g --defined-only "$work/probe-disabled.o")"
test -z "$(nm -g --defined-only "$work/blade-disabled.o")"

"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  "$root/test_rf.c" "$root/exp3.c" "$root/exp3_link.c" "$root/exp3_hil.c" \
  "$root/openi6x_adapter.c" -o "$work/rf"
ASAN_OPTIONS=detect_leaks=0 "$work/rf"
"$cc" -std=c99 -Wall -Wextra -Werror -DEXP3_ENABLED=1 \
  "$root/test_vectors.c" "$root/exp3.c" -o "$work/vectors"
python3 "$root/check_vectors.py" "$work/vectors"
printf '#include <stdint.h>\nuint32_t HAL_GetTick(void);\nuint32_t HAL_GetUIDw0(void);\nuint32_t HAL_GetUIDw1(void);\nstruct FakeSysTick { uint32_t VAL; };\nextern struct FakeSysTick fake_systick;\n#define SysTick (&fake_systick)\n' > "$work/main.h"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address \
  -DEXP3_ENABLED=1 -DEXP3_PWM_PROBE_ENABLED=1 -DEXP3_AUTO_SESSION_ENABLED=1 \
  -I"$work" -I"$root" \
  "$root/test_auto_board.c" "$root/exp3.c" "$root/exp3_link.c" \
  "$root/exp3_hil.c" "$root/exp3_auto_session.c" "$root/exp3_probe.c" \
  "$root/stm32_adapter.c" -o "$work/auto-board"
ASAN_OPTIONS=detect_leaks=0 "$work/auto-board"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address \
  -DEXP3_ENABLED=1 -DEXP3_PWM_PROBE_ENABLED=1 -DEXP3_AUTO_SESSION_ENABLED=1 \
  "$root/test_auto_radio.c" "$root/exp3.c" "$root/exp3_link.c" \
  "$root/exp3_hil.c" "$root/exp3_auto_session.c" \
  "$root/openi6x_adapter.c" -o "$work/auto-radio"
ASAN_OPTIONS=detect_leaks=0 "$work/auto-radio"
"$cc" -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address -DEXP3_ENABLED=1 \
  -I"$work" -I"$root" -I"$root/../../control_chassis/Core/Inc" \
  "$root/test_sens.c" "$root/exp3.c" "$root/exp3_link.c" "$root/exp3_hil.c" \
  "$root/stm32_adapter.c" "$root/../../control_chassis/Core/UserDev/sens_ibus_responder.c" \
  -o "$work/sens"
ASAN_OPTIONS=detect_leaks=0 "$work/sens"
