cmake_minimum_required(VERSION 3.25)
project(miniredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MINIREDIS_BUILD_TESTS "Build the test suite" ON)
set(MINIREDIS_SANITIZE "" CACHE STRING "Sanitizer to enable: address, thread, or empty")

# Applied globally so GoogleTest is instrumented too; TSan reports false positives otherwise.
if(MINIREDIS_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
elseif(MINIREDIS_SANITIZE STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(NOT MINIREDIS_SANITIZE STREQUAL "")
    message(FATAL_ERROR "Unknown MINIREDIS_SANITIZE value: ${MINIREDIS_SANITIZE}")
endif()

find_package(Threads REQUIRED)

add_library(miniredis_warnings INTERFACE)
target_compile_options(miniredis_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast)

add_library(miniredis_core STATIC
    src/persistence/aof_loader.cpp
    src/persistence/aof_writer.cpp
    src/protocol/inline_parser.cpp
    src/protocol/reply.cpp
    src/protocol/resp.cpp
    src/server/command_dispatcher.cpp
    src/server/config.cpp
    src/server/connection.cpp
    src/server/net.cpp
    src/server/server.cpp
    src/server/thread_pool.cpp
    src/store/expiry_sweeper.cpp
    src/store/store.cpp)
target_include_directories(miniredis_core PUBLIC src)
target_link_libraries(miniredis_core PUBLIC Threads::Threads PRIVATE miniredis_warnings)

add_executable(miniredis src/main.cpp)
target_link_libraries(miniredis PRIVATE miniredis_core miniredis_warnings)

if(MINIREDIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
