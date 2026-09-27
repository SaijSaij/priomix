# SPDX-License-Identifier: GPL-3.0-only
find_package(GTest REQUIRED)

add_executable(priomix-tests
    config_store_test.cpp
    device_id_test.cpp
    json_test.cpp
    priority_engine_test.cpp)
target_link_libraries(priomix-tests PRIVATE priomix-core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(priomix-tests)
