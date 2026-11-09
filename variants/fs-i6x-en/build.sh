#!/usr/bin/env bash
# Build the independent English snapshot. Does not program a device.
set -euo pipefail
variant=$(cd "$(dirname "$0")" && pwd)
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export MOWER_RADIO_BUILD_DIR="${MOWER_RADIO_BUILD_DIR:-$variant/build/en}"
export MOWER_UI_PREVIEW_DIR="${MOWER_UI_PREVIEW_DIR:-$variant/build/preview}"
mkdir -p "$MOWER_RADIO_BUILD_DIR" "$MOWER_UI_PREVIEW_DIR"
"$variant/source/tools/build_mower_user_ui.sh" 2>&1 | tee "$MOWER_RADIO_BUILD_DIR/build.log"
