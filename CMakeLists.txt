cmake_minimum_required(VERSION 3.24)
project(MuTap VERSION 0.1.0 LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Shared real FFT (DspTap) — the Ooura wrapper with optional CMSIS-DSP Helium /
# Apple vDSP float32 backends, formerly vendored here (include/mutap/fft.h +
# third_party/{ooura,cmsis-dsp}). Now consumed from the tap/dsptap submodule as
# tap::dsp, which carries the same numeric contract and selects the per-target
# backend itself (CMSIS on the bare-metal M55, Ooura elsewhere).
# Override with -DTAP_DSP_FFT_CMSIS=OFF / -DTAP_DSP_FFT_ACCELERATE=ON.
#
# APPLE float32 USES OOURA, NOT vDSP — DspTap defaults vDSP ON for Apple and
# MuTap deliberately turns it back off. Two measured reasons (tap/MuTap#31):
#
#   1. Accuracy. On spectra with exactly-empty bins — an on-bin tone, which is
#      what the G.168 tone rows drive the chain with — the vDSP kernel selected
#      by 64-byte buffer alignment is far less accurate than Ooura. Measured
#      against a double-precision reference on Apple M1, MEDIAN per-bin relative
#      error at N=2048: vDSP 0.65, Ooura 1.1e-07. Broadband and off-bin material
#      show no such gap, which is why the peak-normalized parity gate never saw
#      it. The compliance tone rows fail on that kernel and pass on Ooura.
#
#   2. Contract. Apple's own vDSP.h states the routines are "free to rearrange
#      calculations for better performance", that "rounding errors will often be
#      different when operations are rearranged", and that they are "not expected
#      to conform to IEEE 754". Which kernel runs has been observed to depend on
#      buffer alignment, and nothing documented prevents that from changing
#      again. A certified compliance claim cannot rest on it.
#
# This is a MuTap policy choice, not a DspTap defect: vDSP remains the right
# default for consumers who want the ~3x and can tolerate that latitude. Setting
# it here (not FORCE) so an explicit -DTAP_DSP_FFT_ACCELERATE=ON still wins for
# anyone who wants to measure it.
# ------------------------------------------------------------------------------

set(TAP_DSP_FFT_ACCELERATE OFF CACHE BOOL
    "Route the float32 FFT through Apple vDSP (MuTap defaults this OFF; see #31)")

add_subdirectory(submodules/dsptap)

# ------------------------------------------------------------------------------
# The library proper (header-only)
# ------------------------------------------------------------------------------

add_library(mutap INTERFACE)
add_library(MuTap::MuTap ALIAS mutap)
# The family-wide alias, per the namespace convention in taphouse's README: one
# `tap::<library>` sub-namespace per repo, and the CMake alias matches it. The
# older spelling above stays so existing consumers keep working; prefer
# `tap::mu` in new code.
add_library(tap::mu ALIAS mutap)
target_include_directories(mutap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(mutap INTERFACE cxx_std_20)
target_link_libraries(mutap INTERFACE tap::dsp)

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

# Scenario dump for the ML echo-cancellation benchmark harness
# (tools/ml/run_benchmark.py; see tools/ml/README.md).
option(MUTAP_BUILD_ML_TOOLS "Build the ML benchmark scenario dump" OFF)
if(MUTAP_BUILD_ML_TOOLS)
    add_subdirectory(tools/ml)
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
