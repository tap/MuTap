# Cross-algorithm AEC comparison harness (docs/aec-comparison.md).
#
# Off by default and never a CI gate — like SampleRateTap's
# SRT_BUILD_COMPARE_BENCH, this pulls in third-party echo cancellers and
# is a host-only measurement tool. Configure with -DMUTAP_BUILD_COMPARE=ON.
#
# Each third-party subject is a separate feature that degrades gracefully:
#   -DMUTAP_COMPARE_SPEEX=ON   builds the Speex MDF subject (sources are
#                              cloned/pointed to via MUTAP_SPEEXDSP_DIR).
#   -DMUTAP_COMPARE_WEBRTC=ON  builds the WebRTC AEC3 subject against a
#                              prebuilt webrtc-audio-processing (meson);
#                              point MUTAP_WEBRTC_DIR at its source and
#                              MUTAP_WEBRTC_BUILD at its meson builddir.
# With neither, the tool still compares the three MuTap variants — useful
# for tracking our own linear-vs-chain and f32-vs-f64 gaps over time.

add_executable(mutap_aec_compare
    compare_main.cpp
    aec_backend.cpp
    backends/mutap_backend.cpp)

# A host-only measurement tool: -Wall/-Wextra, but not the library's
# strict -Wconversion (the driver's size_t<->double window arithmetic is
# deliberate and would otherwise need a cast on every line).
target_link_libraries(mutap_aec_compare PRIVATE MuTap::MuTap)
target_compile_options(mutap_aec_compare PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
target_include_directories(mutap_aec_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mutap_aec_compare PRIVATE cxx_std_20)

# ----------------------------------------------------------------------
# Speex MDF subject
# ----------------------------------------------------------------------
option(MUTAP_COMPARE_SPEEX "Build the Speex MDF subject into the comparison tool" OFF)
if(MUTAP_COMPARE_SPEEX)
    if(NOT DEFINED MUTAP_SPEEXDSP_DIR OR NOT EXISTS "${MUTAP_SPEEXDSP_DIR}/libspeexdsp/mdf.c")
        message(FATAL_ERROR
            "MUTAP_COMPARE_SPEEX=ON needs MUTAP_SPEEXDSP_DIR pointing at a "
            "speexdsp checkout (git clone https://github.com/xiph/speexdsp).")
    endif()
    # Compile only the MDF echo canceller and its FFT/DSP deps directly —
    # no autotools. FLOATING_POINT + the kiss_fft backend (USE_KISS_FFT)
    # is the portable, dependency-free configuration.
    add_library(mutap_speexdsp STATIC
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp/mdf.c
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp/fftwrap.c
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp/kiss_fft.c
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp/kiss_fftr.c
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp/smallft.c)
    target_include_directories(mutap_speexdsp PUBLIC
        ${MUTAP_SPEEXDSP_DIR}/include
        ${MUTAP_SPEEXDSP_DIR}/libspeexdsp
        ${CMAKE_CURRENT_SOURCE_DIR}/speex_config)
    # HAVE_CONFIG_H routes the sources through speex_config/config.h, which
    # selects the float + KISS-FFT build (no autotools, no SIMD deps).
    target_compile_definitions(mutap_speexdsp PRIVATE HAVE_CONFIG_H)
    # Third-party C: no MuTap warning flags.
    target_link_libraries(mutap_aec_compare PRIVATE mutap_speexdsp)
    target_sources(mutap_aec_compare PRIVATE backends/speex_backend.cpp)
    target_compile_definitions(mutap_aec_compare PRIVATE MUTAP_COMPARE_HAVE_SPEEX=1)
endif()

# ----------------------------------------------------------------------
# WebRTC AEC3 subject
# ----------------------------------------------------------------------
option(MUTAP_COMPARE_WEBRTC "Build the WebRTC AEC3 subject into the comparison tool" OFF)
if(MUTAP_COMPARE_WEBRTC)
    # Resolve webrtc-audio-processing + its abseil deps entirely through
    # pkg-config (the installed .pc lists every transitive abseil lib).
    # Point MUTAP_WEBRTC_PC_PATH at the pkgconfig dirs of the
    # webrtc-audio-processing and abseil install prefixes, e.g.
    #   -DMUTAP_WEBRTC_PC_PATH="<webrtc>/lib/x86_64-linux-gnu/pkgconfig:<absl>/lib/pkgconfig"
    # (see docs/aec-comparison.md for the one-time build recipe).
    find_package(PkgConfig REQUIRED)
    if(DEFINED MUTAP_WEBRTC_PC_PATH)
        set(ENV{PKG_CONFIG_PATH} "${MUTAP_WEBRTC_PC_PATH}:$ENV{PKG_CONFIG_PATH}")
    endif()
    pkg_check_modules(WEBRTC REQUIRED IMPORTED_TARGET webrtc-audio-processing-2)
    target_sources(mutap_aec_compare PRIVATE backends/webrtc_backend.cpp)
    target_link_libraries(mutap_aec_compare PRIVATE PkgConfig::WEBRTC)
    target_compile_definitions(mutap_aec_compare PRIVATE MUTAP_COMPARE_HAVE_WEBRTC=1)
endif()
