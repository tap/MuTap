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
