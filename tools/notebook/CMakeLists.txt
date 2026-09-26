# Measurement dumps for the notebooks. Host-side, double precision only —
# never part of the emulated-target builds (the options default OFF and the
# notebook builders / drivers switch them on). Both reuse the test suite's
# scenario machinery verbatim (tests/support/*.h, tests/fixtures/*.h) so a
# notebook's or a doc's numbers come from the same meters and loops the
# assertions gate.

# The Stage 4 measurement dump for the ITU compliance proof notebook.
if(MUTAP_BUILD_ITU_DUMP)
    add_executable(itu_dump itu_dump.cpp)
    target_link_libraries(itu_dump PRIVATE MuTap::MuTap mutap_warnings)
    target_include_directories(itu_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)
endif()

# The karaoke gain-ramp dump: the simulated loop of
# tests/support/decorrelated_loop.h under a 1 dB / 2 s gain ramp, for the
# offline howl criterion (tools/notebook/karaoke_audible.py).
if(MUTAP_BUILD_KARAOKE_DUMP)
    add_executable(karaoke_ramp_dump karaoke_ramp_dump.cpp)
    target_link_libraries(karaoke_ramp_dump PRIVATE MuTap::MuTap mutap_warnings)
    target_include_directories(karaoke_ramp_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)
endif()
