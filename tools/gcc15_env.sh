# Source from build entry points. Native host harnesses keep their host compiler.
export ARM_GNU_TOOLCHAIN_ROOT="${ARM_GNU_TOOLCHAIN_ROOT:-$HOME/.local/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi}"
if [[ ! -x "$ARM_GNU_TOOLCHAIN_ROOT/bin/arm-none-eabi-gcc" ]]; then
    echo "GCC 15.3 is missing: $ARM_GNU_TOOLCHAIN_ROOT. Run ./tools/install_gcc15.sh or set ARM_GNU_TOOLCHAIN_ROOT." >&2
    return 1
fi
if [[ $("$ARM_GNU_TOOLCHAIN_ROOT/bin/arm-none-eabi-gcc" -dumpfullversion) != 15.3.1 ]]; then
    echo 'This project requires Arm GNU 15.3.Rel1 (GCC 15.3.1).' >&2
    return 1
fi
export PATH="$ARM_GNU_TOOLCHAIN_ROOT/bin:$PATH"
