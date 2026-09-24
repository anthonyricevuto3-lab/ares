cmake_minimum_required(VERSION 3.20)

project(ARES
    VERSION 0.2.0
    DESCRIPTION "Autonomous Resilient Embedded Spacecraft System"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(ARES_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(ARES_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ARES_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
        check_cxx_source_compiles("int main() { return 0; }" ARES_SANITIZERS_LINK)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(NOT ARES_SANITIZERS_LINK)
            message(FATAL_ERROR
                "ARES_ENABLE_SANITIZERS is ON, but ${CMAKE_CXX_COMPILER_ID} cannot link "
                "AddressSanitizer and UndefinedBehaviorSanitizer. On MSYS2 UCRT64 this "
                "usually means libasan and libubsan are not shipped. Use Clang or GCC on "
                "Linux, or configure with -DARES_ENABLE_SANITIZERS=OFF.")
        endif()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    elseif(MSVC)
        string(REGEX REPLACE "/RTC[^ ]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO /fsanitize=address)
    else()
        message(FATAL_ERROR "ARES_ENABLE_SANITIZERS is not supported for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

find_package(Threads REQUIRED)

function(ares_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(ARES_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor)
        if(ARES_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(ares_core STATIC
    src/core/clock.cpp
    src/core/logger.cpp
    src/core/periodic_task.cpp
    src/core/task_supervisor.cpp
)
target_include_directories(ares_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_core PUBLIC Threads::Threads)
ares_configure_target(ares_core)

add_library(ares_hardware INTERFACE)
target_include_directories(ares_hardware INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_hardware INTERFACE ares_core)

add_library(ares_flight STATIC
    src/flight/mode_machine.cpp
    src/flight/example_tasks.cpp
    src/flight/executive.cpp
)
target_include_directories(ares_flight PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_flight PUBLIC ares_hardware)
ares_configure_target(ares_flight)

add_library(ares_simulation STATIC
    src/simulation/spacecraft_model.cpp
    src/simulation/sensors.cpp
)
target_include_directories(ares_simulation PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_simulation PUBLIC ares_hardware)
ares_configure_target(ares_simulation)

add_library(ares_app STATIC
    src/application.cpp
)
target_include_directories(ares_app PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_app PUBLIC ares_flight ares_simulation)
ares_configure_target(ares_app)

add_executable(ares src/main.cpp)
target_link_libraries(ares PRIVATE ares_app)
ares_configure_target(ares)

include(CTest)
if(BUILD_TESTING)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
    )
    FetchContent_MakeAvailable(googletest)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25" AND TARGET gtest AND TARGET gtest_main)
        set_target_properties(gtest gtest_main PROPERTIES SYSTEM TRUE)
    endif()

    add_executable(ares_tests
        tests/unit/bounded_log_test.cpp
        tests/unit/clock_test.cpp
        tests/unit/logger_test.cpp
        tests/unit/deadline_monitor_test.cpp
        tests/unit/event_log_test.cpp
        tests/unit/periodic_task_test.cpp
        tests/unit/mode_machine_test.cpp
        tests/unit/executive_test.cpp
        tests/unit/example_tasks_test.cpp
        tests/unit/launch_options_test.cpp
        tests/integration/task_supervisor_test.cpp
        tests/integration/boot_sequence_test.cpp
        tests/integration/application_test.cpp
        tests/integration/mission_fault_test.cpp
        tests/unit/sensor_sim_test.cpp
        tests/unit/navigation_solution_test.cpp
        tests/unit/power_thermal_test.cpp
        tests/integration/simulated_spacecraft_test.cpp
    )
    target_link_libraries(ares_tests PRIVATE ares_app GTest::gtest_main)
    ares_configure_target(ares_tests)

    include(GoogleTest)
    gtest_discover_tests(ares_tests
        PROPERTIES TIMEOUT 30
        DISCOVERY_TIMEOUT 60
    )
    add_test(NAME ares_smoke COMMAND ares --duration-ms 100)
    set_tests_properties(ares_smoke PROPERTIES TIMEOUT 30)
endif()

message(STATUS "ARES ${PROJECT_VERSION} sanitizers=${ARES_ENABLE_SANITIZERS}")
