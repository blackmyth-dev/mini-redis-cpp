cmake_minimum_required(VERSION 3.20)
project(cpp_edge_gateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(GATEWAY_BUILD_TESTS "Build unit tests" ON)
find_package(Threads REQUIRED)

add_library(gateway_core
    src/core/key_value_store.cpp
    src/http/http_parser.cpp
    src/http/http_server.cpp
    src/redis/command_parser.cpp
    src/redis/tcp_server.cpp
    src/storage/snapshot_store.cpp
)
target_include_directories(gateway_core PUBLIC include)
target_link_libraries(gateway_core PUBLIC Threads::Threads)
target_compile_options(gateway_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

add_executable(edge_gateway apps/edge_gateway_main.cpp)
target_link_libraries(edge_gateway PRIVATE gateway_core)

if(GATEWAY_BUILD_TESTS)
    enable_testing()
    add_executable(gateway_tests tests/gateway_tests.cpp)
    target_link_libraries(gateway_tests PRIVATE gateway_core)
    add_test(NAME gateway_tests COMMAND gateway_tests)
endif()
