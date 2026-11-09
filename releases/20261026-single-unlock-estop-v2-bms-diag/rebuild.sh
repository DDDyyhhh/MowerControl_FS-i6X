#!/usr/bin/env bash
set -euo pipefail
release_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_root=$(mktemp -d /tmp/mower-bms-diag-rebuild-XXXXXX)
tar -xzf "$release_dir/buildable-source.tar.gz" -C "$build_root"
source_dir="$build_root/mower-source"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" "$source_dir/tools/build_blade_ok.sh"
echo "源码复建目录：$source_dir"
