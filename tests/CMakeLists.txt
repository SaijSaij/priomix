# SPDX-License-Identifier: GPL-3.0-only
find_package(GTest REQUIRED)
include(GoogleTest)

# ---- Unit tests: pure logic, no running services needed ---------------------------------
add_executable(priomix-tests
    config_store_test.cpp
    device_id_test.cpp
    json_test.cpp
    priority_engine_test.cpp)
target_link_libraries(priomix-tests PRIVATE priomix-core GTest::gtest_main)
gtest_discover_tests(priomix-tests PROPERTIES LABELS unit)

# ---- Integration tests: real audio path in an isolated PipeWire session -----------------
# Every test starts its own D-Bus, PipeWire, WirePlumber (profile "policy", no hardware
# monitors) and priomix-daemon, so the desktop session is never touched. Tests are skipped
# if those programs are missing. Run only them with `ctest -L integration`, or exclude them
# (e.g. in a build chroot) with `ctest -LE integration`.
option(PRIOMIX_BUILD_INTEGRATION_TESTS "Build the PipeWire integration tests" ON)
if(PRIOMIX_BUILD_INTEGRATION_TESTS)
    # Imported targets are directory scoped, so look them up here as well.
    find_package(PkgConfig REQUIRED)
    find_package(sdbus-c++ 2.0 REQUIRED)
    pkg_check_modules(TEST_PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    add_executable(priomix-integration-tests
        integration/audio_path_test.cpp
        integration/pw_graph.cpp
        integration/test_session.cpp)
    target_compile_definitions(priomix-integration-tests PRIVATE
        PRIOMIX_DAEMON_PATH="$<TARGET_FILE:priomix-daemon>")
    target_compile_options(priomix-integration-tests PRIVATE -Wall -Wextra
                           -Wno-missing-field-initializers)
    target_link_libraries(priomix-integration-tests PRIVATE
        PkgConfig::TEST_PIPEWIRE SDBusCpp::sdbus-c++ GTest::gtest_main)
    add_dependencies(priomix-integration-tests priomix-daemon)
    gtest_discover_tests(priomix-integration-tests
        PROPERTIES LABELS integration TIMEOUT 60)
endif()
