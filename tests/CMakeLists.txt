include(FetchContent)

# Threads are optional: bare-metal targets (e.g. Cortex-M55) have no
# std::thread, so GoogleTest is built single-threaded there. The probe is
# skipped entirely on bare metal: newlib ships pthread.h/regex.h stub
# headers that make POSIX feature detection succeed spuriously.
if(NOT MUTAP_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
if(MUTAP_BARE_METAL)
    # Value-checked feature macros only: GTEST_HAS_DEATH_TEST is tested with
    # #ifdef in gtest 1.14, so it must stay undefined (no GTEST_OS_* is
    # detected on bare metal, which disables it).
    add_compile_definitions(GTEST_HAS_POSIX_RE=0 GTEST_HAS_STREAM_REDIRECTION=0
                            GTEST_HAS_FILE_SYSTEM=0)
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # Commit pin, not the movable tag: tags can be re-pointed upstream.
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
    FIND_PACKAGE_ARGS NAMES GTest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(mutap_tests
    test_fdaf.cpp
    test_fd_kalman.cpp
    test_convergence_stats.cpp
    test_lpc.cpp
    test_pem_afc.cpp
    test_adaptation_control.cpp
    test_closed_loop.cpp
    test_rir_fixtures.cpp
    test_aec.cpp
    test_itu_signals.cpp
    test_postfilter.cpp
    test_itu_echo.cpp
    test_itu_doubletalk.cpp
    test_itu_dynamics.cpp
    test_g168.cpp
    test_float32.cpp
    test_nn_suppressor.cpp
    test_fft_engine_contract.cpp)
target_link_libraries(mutap_tests PRIVATE
    MuTap::MuTap
    mutap_warnings)

# The vendored FAUST material (third_party/faust/: the anti-howl PoC's
# reference reverb and baseline suppressor) is host-only: its generated
# classes carry 256-512 KB class-static oscillator tables and belong to no
# embedded profile, so the cross and bare-metal builds do not compile it, and
# neither emulated selection (bare_metal_main.cpp's filter, TEST_FILTER below)
# names its FaustVendored suite.
if(NOT MUTAP_BARE_METAL AND NOT CMAKE_CROSSCOMPILING)
    target_sources(mutap_tests PRIVATE test_faust_vendored.cpp)
    target_include_directories(mutap_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/faust)
endif()

if(MUTAP_BARE_METAL)
    # One-shot mode (Cortex-M55 under qemu-system-arm): a custom main bakes
    # in the emulation-appropriate test selection (no argv on the target),
    # and the run is judged on gtest's summary text because semihosting does
    # not reliably propagate exit codes through the emulator.
    target_sources(mutap_tests PRIVATE bare_metal_main.cpp)
    target_link_libraries(mutap_tests PRIVATE GTest::gtest)
    add_test(NAME mutap_tests_emulated COMMAND mutap_tests)
    set_tests_properties(mutap_tests_emulated PROPERTIES
        PASS_REGULAR_EXPRESSION "MUTAP_TESTS_COMPLETE rc=0"
        FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
        TIMEOUT 1800)
else()
    target_link_libraries(mutap_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    # Hosted cross targets run under an emulator (Hexagon under
    # qemu-hexagon): the full suite measured ~44 minutes of TCG, almost all
    # of it the double-typed adaptive suites — target-independent math
    # already covered on every host platform. Run the same emulation-sized
    # selection as the Cortex-M55 leg (tests/bare_metal_main.cpp): the
    # float32 typed suites, the double Kalman core, the LP/conditioning
    # suites, the float closed-loop canaries (one seed each; the claims are
    # the host-only *_host_test suites, see tests/support/rooms.h) and the
    # validation/contract tests. (The FFT suites left with the FFT: they run
    # in DspTap's own CI.)
    set(mutap_emulated_selection "")
    if(CMAKE_CROSSCOMPILING)
        set(mutap_emulated_selection TEST_FILTER
            "fdaf_test/0.*:FdafCrossPrecision.*:FdafConfigValidation.*:FdafRtContract.*:fd_kalman_test/0.*:fd_kalman_test/1.*:kalman_loop_test/0.*:FdKalmanConfigValidation.*:FdKalmanRtContract.*:Levinson.*:LpcPredictor.*:SpeechPredictor.*:WarpedLpcPredictor.*:PredictorConfigValidation.*:pem_afc_test/0.*:PemAfcConfigValidation.*:PemAfcRtContract.*:closed_loop_test/0.*:burst_test/0.*:aec_test/0.*:AdaptationControlConfigValidation.*:nn_suppressor_test/0.*:NnSuppressorCrossPrecision.*:NnChainFloat32.*:FftEngineContract.*")
    endif()
    gtest_discover_tests(mutap_tests DISCOVERY_TIMEOUT 120 ${mutap_emulated_selection}
                         PROPERTIES TIMEOUT 900)
endif()

# The bit-identity gate for every DspTap pin bump (tests/fingerprint_harness.cpp):
# one FNV-1a fingerprint per (component, profile) over a fixed synthetic corpus,
# compared against the committed expectation of the CI leg this build is
# (tests/fingerprints/<leg>.txt, chosen by MUTAP_FINGERPRINT_LEG). A plain
# executable with its own main — no gtest — so the branchless-parity CI job can
# also compile the same file standalone. Registered as a test on every target;
# on bare metal the pass criterion is a printed line, as for
# mutap_tests_emulated, because semihosting does not reliably carry the exit
# code.
#
# Determinism holds per (host, compiler, flags, C library), so there is no
# expectation for an arbitrary developer machine: with MUTAP_FINGERPRINT_LEG
# empty (the default) the harness prints its lines and compares nothing. Every
# CI leg that runs the harness sets its leg id; a local build reproduces a leg
# by setting the same id with the same toolchain.
set(MUTAP_FINGERPRINT_LEG "" CACHE STRING
    "CI leg whose committed fingerprints (tests/fingerprints/<leg>.txt) mutap_fingerprint must reproduce; empty = print only")
set(_fp_generated "${CMAKE_CURRENT_BINARY_DIR}/fingerprint_generated")
set(_fp_lines "")
set(_fp_count 0)
set(_fp_source "")
if(MUTAP_FINGERPRINT_LEG)
    set(_fp_file "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints/${MUTAP_FINGERPRINT_LEG}.txt")
    if(NOT MUTAP_FINGERPRINT_LEG MATCHES "^[a-z0-9-]+$" OR NOT EXISTS "${_fp_file}")
        file(GLOB _fp_known RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints"
             "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints/*.txt")
        string(REPLACE ".txt" "" _fp_known "${_fp_known}")
        message(FATAL_ERROR "MUTAP_FINGERPRINT_LEG='${MUTAP_FINGERPRINT_LEG}' names no file "
                            "tests/fingerprints/<leg>.txt; known legs: ${_fp_known}")
    endif()
    # Editing the expectation re-runs the configure step, so a re-recorded
    # file is compiled in on the next build.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_fp_file}")
    file(STRINGS "${_fp_file}" _fp_raw)
    foreach(_fp_line IN LISTS _fp_raw)
        string(REGEX REPLACE "\r$" "" _fp_line "${_fp_line}") # a CRLF checkout
        if(_fp_line MATCHES "^#" OR _fp_line MATCHES "^[ \t]*$")
            continue()
        endif()
        # A pasted line keeps nothing but the fingerprint: a CI timestamp or
        # ctest's "238: " prefix left in the file fails here, not as a
        # mismatch.
        if(NOT _fp_line MATCHES "^FINGERPRINT [a-z_]+ (float|double) [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
            message(FATAL_ERROR "tests/fingerprints/${MUTAP_FINGERPRINT_LEG}.txt: not a fingerprint line "
                                "('FINGERPRINT <component> <float|double> <16 hex digits>'): '${_fp_line}'")
        endif()
        string(APPEND _fp_lines "        \"${_fp_line}\",\n")
        math(EXPR _fp_count "${_fp_count} + 1")
    endforeach()
    set(_fp_source "tests/fingerprints/${MUTAP_FINGERPRINT_LEG}.txt")
endif()
file(CONFIGURE OUTPUT "${_fp_generated}/mutap_fingerprint_expected.h" CONTENT [=[
// Generated by tests/CMakeLists.txt for MUTAP_FINGERPRINT_LEG="@MUTAP_FINGERPRINT_LEG@"
// from tests/fingerprints/<leg>.txt (no leg: no lines). Do not edit; edit the .txt.
#pragma once
#include <array>
#include <string_view>

namespace mutap_fingerprint_expected {
    inline constexpr std::string_view k_leg    = "@MUTAP_FINGERPRINT_LEG@";
    inline constexpr std::string_view k_source = "@_fp_source@";
    inline constexpr std::array<std::string_view, @_fp_count@> k_lines{{
@_fp_lines@    }};
} // namespace mutap_fingerprint_expected
]=] @ONLY)
add_executable(mutap_fingerprint fingerprint_harness.cpp)
target_include_directories(mutap_fingerprint PRIVATE "${_fp_generated}")
target_link_libraries(mutap_fingerprint PRIVATE
    MuTap::MuTap
    mutap_warnings)
add_test(NAME mutap_fingerprint COMMAND mutap_fingerprint)
set_tests_properties(mutap_fingerprint PROPERTIES TIMEOUT 900)
if(MUTAP_FINGERPRINT_LEG)
    # Pass only on the verdict line naming this leg, and fail on the failure
    # line, on every target (the exit code is 1 on a mismatch too, but a
    # PASS_REGULAR_EXPRESSION is the one criterion bare metal honours).
    set_tests_properties(mutap_fingerprint PROPERTIES
        PASS_REGULAR_EXPRESSION "FINGERPRINT_PASS leg=${MUTAP_FINGERPRINT_LEG} "
        FAIL_REGULAR_EXPRESSION "FINGERPRINT_FAIL")
elseif(MUTAP_BARE_METAL)
    set_tests_properties(mutap_fingerprint PROPERTIES
        PASS_REGULAR_EXPRESSION "FINGERPRINT_COMPLETE")
endif()
