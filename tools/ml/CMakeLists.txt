# Scenario dump for the ML echo-cancellation benchmark harness. Host-side,
# double precision only — never part of the emulated-target builds (the
# option defaults OFF; tools/ml/run_benchmark.py switches it on).
add_executable(aec_scenario_dump aec_scenario_dump.cpp)
target_link_libraries(aec_scenario_dump PRIVATE MuTap::MuTap mutap_warnings)
# Reuses the test suite's material generators and echo simulator verbatim
# (tests/support/*.h, tests/fixtures/*.h) so benchmark signals are the test
# suite's signals.
target_include_directories(aec_scenario_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)
add_executable(nn_infer nn_infer.cpp)
target_link_libraries(nn_infer PRIVATE MuTap::MuTap mutap_warnings)

# WebRTC AEC3 baseline for the benchmark/notebook — only when pkg-config
# finds the freedesktop webrtc-audio-processing >= 1.0 (the versions that
# carry AEC3; distro 0.3.x packages are the legacy canceller and are
# deliberately not accepted). Third-party headers, so no mutap_warnings.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(WAP IMPORTED_TARGET webrtc-audio-processing-2)
    if(NOT WAP_FOUND)
        pkg_check_modules(WAP IMPORTED_TARGET webrtc-audio-processing-1)
    endif()
    if(WAP_FOUND)
        add_executable(webrtc_aec3_infer webrtc_aec3_infer.cpp)
        target_link_libraries(webrtc_aec3_infer PRIVATE PkgConfig::WAP)
        # The .pc file's -I stops at include/webrtc-audio-processing-N, but
        # the headers also want the abseil headers installed one level up
        # (include/absl) when webrtc was built with its bundled abseil.
        foreach(wap_inc ${WAP_INCLUDE_DIRS})
            get_filename_component(wap_inc_parent ${wap_inc} DIRECTORY)
            target_include_directories(webrtc_aec3_infer PRIVATE ${wap_inc_parent})
        endforeach()
    endif()
endif()
