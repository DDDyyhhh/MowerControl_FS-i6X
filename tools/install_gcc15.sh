#!/usr/bin/env bash
# Install the pinned Linux toolchain in the user's directory, without sudo.
set -euo pipefail
version=15.3.rel1
bundle="arm-gnu-toolchain-${version}-x86_64-arm-none-eabi"
expected_sha256=563bebb2b97d53382b956d6ee1fe61e2cae26699901417234a37df505ef9b5fa
install_dir="${ARM_GNU_TOOLCHAIN_ROOT:-$HOME/.local/opt/$bundle}"

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo 'This installer supports Linux x86_64. Install Arm GNU 15.3.Rel1 for your host and set ARM_GNU_TOOLCHAIN_ROOT.' >&2
    exit 1
fi
if [[ -x "$install_dir/bin/arm-none-eabi-gcc" ]] &&
   [[ $("$install_dir/bin/arm-none-eabi-gcc" -dumpfullversion) == 15.3.1 ]]; then
    echo "Already installed: $install_dir (GCC 15.3.1)"
    exit 0
fi
if [[ -e "$install_dir" ]]; then
    echo "Installation path already exists; choose another ARM_GNU_TOOLCHAIN_ROOT: $install_dir" >&2
    exit 1
fi
work=$(mktemp -d /tmp/mower-gcc15-install.XXXXXX)
trap 'rm -rf "$work"' EXIT
archive="${1:-$work/$bundle.tar.xz}"
if [[ $# -eq 0 ]]; then
    curl --fail --location --retry 2 --connect-timeout 15 --max-time 300 \
        --output "$archive" \
        "https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/$version/$bundle.tar.xz"
fi
printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"
[[ $("$work/$bundle/bin/arm-none-eabi-gcc" -dumpfullversion) == 15.3.1 ]]
mkdir -p "$(dirname "$install_dir")"
mv "$work/$bundle" "$install_dir"
echo "Installed: $install_dir (GCC 15.3.1)"
