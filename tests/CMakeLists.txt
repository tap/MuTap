include(FetchContent)

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
    test_fft.cpp
    test_fdaf.cpp
    test_lpc.cpp
    test_pem_afc.cpp
    test_closed_loop.cpp)
target_link_libraries(mutap_tests PRIVATE
    MuTap::MuTap
    mutap_warnings
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(mutap_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
