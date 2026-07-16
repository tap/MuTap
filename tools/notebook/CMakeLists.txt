# The Stage 4 measurement dump for the ITU compliance proof notebook.
# Host-side, double precision only — never part of the emulated-target
# builds (the option defaults OFF and the notebook builder switches it on).
add_executable(itu_dump itu_dump.cpp)
target_link_libraries(itu_dump PRIVATE MuTap::MuTap mutap_warnings)
# The dump reuses the test suite's scenario machinery verbatim
# (tests/support/itu_chain.h et al.) so the notebook's numbers come from
# the same meters the assertions gate.
target_include_directories(itu_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests)
