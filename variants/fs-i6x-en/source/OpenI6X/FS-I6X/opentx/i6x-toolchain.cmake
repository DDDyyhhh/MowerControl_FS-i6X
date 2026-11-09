# Top-level toolchain file for building the I6X firmware on Windows.
# Usage:
#   cmake -S . -B build-i6x -G Ninja -DCMAKE_TOOLCHAIN_FILE=../i6x-toolchain.cmake -DPCB=I6X ...
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT "C:/Users/20147/Downloads/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin" CACHE PATH "arm-none-eabi toolchain bin directory")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
