cmake_minimum_required(VERSION 3.24)
project(MuTap VERSION 0.1.0 LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Ooura FFT (vendored) — provides the extern "C" rdft/rdft_f used by
# include/mutap/fft.h. PIC so MODULE/SHARED consumers (host applications,
# Max externals) can link it.
# ------------------------------------------------------------------------------

add_library(mutap_fft STATIC third_party/ooura/fftsg.c third_party/ooura/fftsg_float.c)
add_library(MuTap::fft ALIAS mutap_fft)
set_target_properties(mutap_fft PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME mutap_fft)

# ------------------------------------------------------------------------------
# The library proper (header-only)
# ------------------------------------------------------------------------------

add_library(mutap INTERFACE)
add_library(MuTap::MuTap ALIAS mutap)
target_include_directories(mutap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(mutap INTERFACE cxx_std_20)
target_link_libraries(mutap INTERFACE mutap_fft)

# ------------------------------------------------------------------------------
# Helium (Cortex-M55 MVE) FFT backend. When ON, the float32 real FFT in
# include/mutap/fft.h routes through the vendored CMSIS-DSP subset
# (third_party/cmsis-dsp, Apache-2.0) instead of Ooura — measured ~3x fewer
# instructions per transform, ~43% off the whole chain on the M55
# (docs/optimization.md). Double is untouched (always Ooura), so the
# double-precision ITU certification is unaffected; on the float32 embedded
# path CMSIS agrees with Ooura to single-precision rounding (not bit-identical).
#
# Default ON for the bare-metal M55 embedded profile (the deployment target),
# OFF everywhere else — desktop, the Max/C-ABI host builds (incl. Apple
# Silicon arm64, which is not this Generic+arm profile), and Hexagon all keep
# Ooura. Force the Ooura path on the M55 with -DMUTAP_FFT_CMSIS=OFF.
# ------------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    set(_mutap_fft_cmsis_default ON) # bare-metal M55 (cmake/arm-cortex-m55-mps3.cmake)
else()
    set(_mutap_fft_cmsis_default OFF)
endif()
option(MUTAP_FFT_CMSIS "Route the float32 FFT through the vendored CMSIS-DSP Helium backend (Arm/Helium only)" ${_mutap_fft_cmsis_default})
if(MUTAP_FFT_CMSIS)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
        message(FATAL_ERROR
            "MUTAP_FFT_CMSIS is an Arm/Helium backend; configure with the "
            "cortex-m55 toolchain (cmake/arm-cortex-m55-mps3.cmake).")
    endif()
    file(GLOB _mutap_cmsis_src CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/Source/*/*.c)
    target_sources(mutap_fft PRIVATE ${_mutap_cmsis_src})
    # PRIVATE for the vendored .c compiles; the same dirs are exposed on the
    # INTERFACE below so consumers' fft.h can resolve "arm_math.h".
    set(_mutap_cmsis_inc
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/PrivateInclude
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/cmsis-core/Include)
    target_include_directories(mutap_fft PRIVATE ${_mutap_cmsis_inc})
    target_include_directories(mutap INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/PrivateInclude>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis-dsp/cmsis-core/Include>)
    target_compile_definitions(mutap INTERFACE MUTAP_FFT_CMSIS)
    message(STATUS "MuTap: float32 FFT backend = CMSIS-DSP Helium")
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)
    option(MUTAP_BUILD_TESTS "Build MuTap tests" ON)
else()
    option(MUTAP_BUILD_TESTS "Build MuTap tests" OFF)
endif()

# Warning flags for this project's own tests/tools; never exported to
# consumers of the INTERFACE library, and never applied to third_party C.
option(MUTAP_WERROR "Treat warnings as errors in MuTap's own targets" OFF)
add_library(mutap_warnings INTERFACE)
target_compile_options(mutap_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(MUTAP_WERROR)
    target_compile_options(mutap_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

if(MUTAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# C ABI shared library for FFI consumers (see notebooks/afc_demo.ipynb).
option(MUTAP_BUILD_CAPI "Build the C ABI shared library" OFF)
if(MUTAP_BUILD_CAPI)
    add_subdirectory(tools/capi)
endif()

# Measurement dump for the ITU compliance proof notebook
# (tools/notebook/build_itu_compliance.py -> notebooks/itu_compliance.ipynb).
option(MUTAP_BUILD_ITU_DUMP "Build the ITU compliance measurement dump" OFF)
if(MUTAP_BUILD_ITU_DUMP)
    add_subdirectory(tools/notebook)
endif()

# Host-only performance baselines (Google Benchmark needs threads);
# see bench/README.md for the workflow. Never a CI performance gate.
option(MUTAP_BUILD_BENCHMARKS "Build MuTap benchmarks" OFF)
if(MUTAP_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# Deterministic instruction-count ratchet workloads (bench/icount): one
# bare-metal binary per scenario, run under QEMU with the counting plugin
# by scripts/icount.py. Builds on any target incl. the cross toolchains.
option(MUTAP_BUILD_ICOUNT_BENCH "Build instruction-count ratchet workloads" OFF)
if(MUTAP_BUILD_ICOUNT_BENCH)
    add_subdirectory(bench/icount)
endif()
