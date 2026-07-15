# Cross-compile MuTap for Qualcomm Hexagon — the third target of the
# one-core/three-targets plan (HANDOFF.md). Triple: hexagon-unknown-linux-musl,
# built with the Codelinaro/QuIC "toolchain for hexagon" (clang + musl sysroot
# + LLVM runtimes): point HEXAGON_TOOLCHAIN_ROOT (cache variable or
# environment) at the unpacked clang+llvm-*-cross-hexagon-unknown-linux-musl
# directory.
#
# Unlike the Cortex-M55 leg this is a hosted Linux target: the stock gtest
# main, ctest discovery and exit codes all work unchanged under qemu-hexagon
# user-mode emulation. Binaries are linked statically (first-class with musl)
# so the emulator needs no sysroot path.
#
# Flags:
#   -mv68                 The QCS8550-class cDSP is V73, but v68 is the newest
#                         rev the shipped sysroot libraries and qemu-hexagon
#                         agree on, and the float32 core has no rev-specific
#                         code. Raise when building against the real SDK.
#   -mhvx -mhvx-length=128b
#                         HVX auto-vectorization for the 128-byte (32 fp32
#                         lane) vector unit — the only vector length on this
#                         generation (HANDOFF.md: 64-byte mode died at V66).
#
# What this leg does NOT cover: VTCM placement, L2 streaming layout and
# FastRPC offload need the proprietary Hexagon SDK and hardware. This build
# pins down compiler correctness (LLVM Hexagon backend + HVX vectorization)
# and the numerics of the core under emulation.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR Hexagon)

if(NOT DEFINED HEXAGON_TOOLCHAIN_ROOT OR HEXAGON_TOOLCHAIN_ROOT STREQUAL "")
    set(HEXAGON_TOOLCHAIN_ROOT "$ENV{HEXAGON_TOOLCHAIN_ROOT}")
endif()
if(HEXAGON_TOOLCHAIN_ROOT STREQUAL "")
    message(FATAL_ERROR
        "Set HEXAGON_TOOLCHAIN_ROOT to the unpacked Codelinaro hexagon "
        "toolchain (clang+llvm-*-cross-hexagon-unknown-linux-musl)")
endif()

set(MUTAP_HEXAGON_TRIPLE hexagon-unknown-linux-musl)

# Newer toolchain releases ship triple-prefixed driver wrappers that know
# their own sysroot; fall back to the bare driver + explicit target.
if(EXISTS "${HEXAGON_TOOLCHAIN_ROOT}/bin/${MUTAP_HEXAGON_TRIPLE}-clang++")
    set(CMAKE_C_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/${MUTAP_HEXAGON_TRIPLE}-clang")
    set(CMAKE_CXX_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/${MUTAP_HEXAGON_TRIPLE}-clang++")
else()
    set(CMAKE_C_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/clang")
    set(CMAKE_CXX_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/clang++")
    set(CMAKE_C_COMPILER_TARGET ${MUTAP_HEXAGON_TRIPLE})
    set(CMAKE_CXX_COMPILER_TARGET ${MUTAP_HEXAGON_TRIPLE})
endif()

set(MUTAP_HEXAGON_ARCH_FLAGS "-mv68 -mhvx -mhvx-length=128b")
set(CMAKE_C_FLAGS_INIT "${MUTAP_HEXAGON_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MUTAP_HEXAGON_ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ctest (and gtest test discovery) run the cross binaries through this.
find_program(MUTAP_QEMU_HEXAGON NAMES qemu-hexagon-static qemu-hexagon)
if(MUTAP_QEMU_HEXAGON)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${MUTAP_QEMU_HEXAGON}")
endif()
