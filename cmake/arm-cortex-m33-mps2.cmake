# Cross-compilation toolchain for Arm Cortex-M33 (bare metal, newlib +
# semihosting), executed on QEMU's MPS2+ AN505 board model. This is the
# Raspberry Pi Pico 2 W (RP2350) class of core — the wake-word plan's named
# embedded target: single-precision FPU only, no FP64, no MVE/Helium. The
# float32 profile is the profile here; anything double is soft-float and
# is excluded from the on-target selection. Ported from RatioTap's
# cmake/arm-cortex-m33-mps2.cmake (which ported it from SampleRateTap's).
#
# Usage:
#   cmake -B build-m33 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
#         -DCMAKE_BUILD_TYPE=MinSizeRel
# with arm-none-eabi-g++ and qemu-system-arm on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

get_filename_component(_mutap_platform "${CMAKE_CURRENT_LIST_DIR}/../platform" ABSOLUTE)
# Same startup as the M55 leg (Armv8-M, shared); the AN505 linker script
# places everything in the board's secure aliases (4 MB code, 4 MB data).
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--specs=rdimon.specs -nostartfiles -Wl,--gc-sections -T${_mutap_platform}/mps2_an505.ld -x c ${_mutap_platform}/armv8m_startup.c -x none")

set(CMAKE_CROSSCOMPILING_EMULATOR
    "qemu-system-arm;-M;mps2-an505;-nographic;-semihosting;-kernel")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# No Helium on the M33: DspTap defaults its CMSIS-DSP Helium FFT backend ON
# for any bare-metal Arm profile, so pin the Ooura float32 path here
# (a plain `set` of the cache entry, which DspTap's option() then respects).
set(TAP_DSP_FFT_CMSIS OFF CACHE BOOL "No MVE on the Cortex-M33: Ooura float32 FFT")

# One-shot CTest mode (no argv on bare metal; see tests/CMakeLists.txt).
set(MUTAP_BARE_METAL ON)
# Single-precision FPU only: the on-target selection drops the long float
# PEM scenarios, whose test harness simulates the room in double (soft-float
# here; ~17 min for one of them under QEMU). See tests/bare_metal_main.cpp.
set(MUTAP_ON_TARGET_SOFT_FP64 ON)
