# The Stage 4 measurement dump for the ITU compliance proof notebook.
# Host-side, double precision only — never part of the emulated-target
# builds (the option defaults OFF and the notebook builder switches it on).
add_executable(itu_dump itu_dump.cpp)
target_link_libraries(itu_dump PRIVATE MuTap::MuTap mutap_warnings)
# The dump reuses the test suite's scenario machinery verbatim
# (tests/support/itu_chain.h et al.) so the notebook's numbers come from
# the same meters the assertions gate.
target_include_directories(itu_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The simulated-device measurement set for the outdoor bring-up recipe
# (build_outdoor_bringup.py -> notebooks/outdoor_bringup.ipynb): same
# option, same host-only policy, same reuse of the test fixtures as the
# source of truth for the simulated device.
add_executable(bringup_dump bringup_dump.cpp)
target_link_libraries(bringup_dump PRIVATE MuTap::MuTap mutap_warnings)
target_include_directories(bringup_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)
