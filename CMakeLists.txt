cmake_minimum_required(VERSION 3.24)
project(cpp_edge_gateway VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GATEWAY_BUILD_RUNTIME "Build protocol adapters and edge_gateway" ON)
option(GATEWAY_BUILD_TESTS "Build domain/application tests" ON)
option(GATEWAY_ENABLE_ASAN "Enable Address/Undefined sanitizers" OFF)
option(GATEWAY_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(GATEWAY_ENABLE_ASAN AND GATEWAY_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled together")
endif()
if(GATEWAY_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(GATEWAY_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)

add_library(gateway_core
    src/application/state_wire_codec.cpp
    src/domain/state_event_bus.cpp
    src/domain/state_store.cpp
    src/infrastructure/file_snapshot_repository.cpp
)
target_include_directories(gateway_core PUBLIC include)
target_link_libraries(gateway_core PUBLIC Threads::Threads)
target_compile_options(gateway_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion
)

if(GATEWAY_BUILD_RUNTIME)
    find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system)
    find_package(PahoMqttCpp CONFIG REQUIRED)
    find_package(vsomeip3 3.4.10 REQUIRED)

    if(TARGET PahoMqttCpp::paho-mqttpp3)
        set(GATEWAY_PAHO_TARGET PahoMqttCpp::paho-mqttpp3)
    elseif(TARGET PahoMqttCpp::paho-mqttpp3-static)
        set(GATEWAY_PAHO_TARGET PahoMqttCpp::paho-mqttpp3-static)
    else()
        message(FATAL_ERROR
            "PahoMqttCpp was found but exported no supported paho-mqttpp3 target")
    endif()
    if(NOT TARGET vsomeip3)
        message(FATAL_ERROR
            "vsomeip3 package was found but the vsomeip3 target is missing")
    endif()

    add_library(gateway_adapters
        src/adapters/http/http_server.cpp
        src/adapters/mqtt/mqtt_bridge.cpp
        src/adapters/someip/vsomeip_service.cpp
        src/adapters/tcp/command_server.cpp
    )
    target_include_directories(gateway_adapters PUBLIC include)
    target_link_libraries(gateway_adapters
        PUBLIC gateway_core
        PRIVATE Boost::system ${GATEWAY_PAHO_TARGET} vsomeip3
    )
    target_compile_options(gateway_adapters PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion
    )

    add_executable(edge_gateway apps/edge_gateway_main.cpp)
    target_link_libraries(edge_gateway PRIVATE gateway_adapters)
endif()

if(GATEWAY_BUILD_TESTS)
    enable_testing()
    add_executable(gateway_tests tests/gateway_tests.cpp)
    target_link_libraries(gateway_tests PRIVATE gateway_core)
    add_test(NAME gateway_tests COMMAND gateway_tests)
endif()
