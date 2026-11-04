include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(miniredis_tests
    store_test.cpp
    thread_pool_test.cpp)
target_link_libraries(miniredis_tests PRIVATE miniredis_core miniredis_warnings GTest::gtest_main)
gtest_discover_tests(miniredis_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 60)
