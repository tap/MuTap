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
