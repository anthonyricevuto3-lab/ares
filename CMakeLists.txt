cmake_minimum_required(VERSION 3.20)

project(ARES
    VERSION 1.0.0
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
option(ARES_ENABLE_COVERAGE "Instrument ARES with compiler coverage counters" OFF)

# Spacecraft integration uses __int128. MSVC and clang-cl are not supported.
if(MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR
        "ARES requires GCC or Clang with __int128. MSVC is not supported. "
        "On Windows, configure from an MSYS2 UCRT64 shell.")
endif()

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/version.hpp.in
    ${PROJECT_BINARY_DIR}/generated/ares/version.hpp
    @ONLY
)

# Release is the compiler's normal optimized build (-O3 and NDEBUG on GCC/Clang).
# Flight checks are explicit. They are not assert(), so NDEBUG does not remove them.
# Fast-math is left off so checked integer paths keep IEEE and wraparound behavior.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-fno-fast-math>"
    )
endif()

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
    else()
        message(FATAL_ERROR "ARES_ENABLE_SANITIZERS is not supported for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if(ARES_ENABLE_COVERAGE)
    if(ARES_ENABLE_SANITIZERS)
        message(FATAL_ERROR
            "ARES_ENABLE_COVERAGE cannot be combined with ARES_ENABLE_SANITIZERS.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "ARES_ENABLE_COVERAGE requires GCC or Clang gcov-compatible instrumentation.")
    endif()
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

find_package(Threads REQUIRED)

# Warnings apply to ARES targets only. GoogleTest is a SYSTEM include and is not compiled with -Werror.
function(ares_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor)
    if(ARES_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
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

add_library(ares_recorder STATIC
    src/recorder/replay.cpp
)
target_include_directories(ares_recorder PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/generated
)
target_link_libraries(ares_recorder PUBLIC ares_flight ares_simulation)
ares_configure_target(ares_recorder)

add_library(ares_app STATIC
    src/application.cpp
)
target_include_directories(ares_app PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ares_app PUBLIC ares_flight ares_simulation ares_recorder)
ares_configure_target(ares_app)

add_executable(ares src/main.cpp)
target_link_libraries(ares PRIVATE ares_app)
ares_configure_target(ares)

add_executable(ares-replay src/recorder/replay_main.cpp)
target_link_libraries(ares-replay PRIVATE ares_recorder)
ares_configure_target(ares-replay)

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
        tests/unit/freshness_test.cpp
        tests/unit/random_source_test.cpp
        tests/unit/spacecraft_integration_test.cpp
        tests/unit/fault_registry_test.cpp
        tests/unit/fault_detection_test.cpp
        tests/unit/fault_policy_test.cpp
        tests/unit/fault_mailbox_test.cpp
        tests/unit/fault_window_test.cpp
        tests/unit/deadline_escalation_test.cpp
        tests/unit/chaos_engine_test.cpp
        tests/unit/recovery_manager_test.cpp
        tests/unit/gps_selector_test.cpp
        tests/integration/task_restart_test.cpp
        tests/unit/sensor_injection_test.cpp
        tests/integration/simulated_spacecraft_test.cpp
        tests/integration/fdir_scenario_test.cpp
        tests/integration/safemode_recovery_test.cpp
        tests/integration/chaos_campaign_test.cpp
        tests/unit/flight_recorder_test.cpp
        tests/unit/replay_test.cpp
        tests/unit/resource_budget_test.cpp
    )
    target_link_libraries(ares_tests PRIVATE ares_app GTest::gtest_main)
    ares_configure_target(ares_tests)

    include(GoogleTest)
    gtest_discover_tests(ares_tests
        PROPERTIES TIMEOUT 60
        DISCOVERY_TIMEOUT 120
    )
    add_test(NAME ares_smoke COMMAND ares --duration-ms 100)
    set_tests_properties(ares_smoke PROPERTIES TIMEOUT 60 LABELS "smoke;integration")
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/ares_apply_labels.cmake"
        CONTENT "set(ARES_BINARY_DIR [[${CMAKE_BINARY_DIR}]])\ninclude([[${CMAKE_CURRENT_SOURCE_DIR}/cmake/ares_test_labels.cmake]])\n"
    )
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
        "${CMAKE_BINARY_DIR}/ares_apply_labels.cmake")
endif()

include(GNUInstallDirs)
install(TARGETS ares ares-replay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    README.md
    CONTRIBUTING.md
    docs/DEMO.md
    docs/RELEASE_NOTES.md
    docs/ARCHITECTURE.md
    docs/PRODUCT_SPEC.md
    docs/ROADMAP.md
    docs/MEMORY.md
    docs/FLIGHT_RECORDER.md
    docs/REPLAY.md
    docs/RECOVERY.md
    docs/FAULT_MODEL.md
    docs/CHAOS_ENGINE.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(CPACK_PACKAGE_NAME "ares")
set(CPACK_PACKAGE_VENDOR "ARES")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Autonomous Resilient Embedded Spacecraft System")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)

find_program(ARES_CLANG_FORMAT NAMES clang-format)
find_program(ARES_CLANG_TIDY NAMES clang-tidy)
add_custom_target(ares-format-check
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCLANG_FORMAT=${ARES_CLANG_FORMAT}
        -DFIX=OFF
        -P ${PROJECT_SOURCE_DIR}/cmake/format_check.cmake
    COMMENT "Checking C++ formatting"
    VERBATIM
)
add_custom_target(ares-format
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCLANG_FORMAT=${ARES_CLANG_FORMAT}
        -DFIX=ON
        -P ${PROJECT_SOURCE_DIR}/cmake/format_check.cmake
    COMMENT "Formatting C++ sources"
    VERBATIM
)
add_custom_target(ares-tidy
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DCLANG_TIDY=${ARES_CLANG_TIDY}
        -P ${PROJECT_SOURCE_DIR}/cmake/tidy.cmake
    COMMENT "Running clang-tidy on ARES sources"
    VERBATIM
)

add_custom_target(ares-coverage
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DCOVERAGE=${ARES_ENABLE_COVERAGE}
        -P ${PROJECT_SOURCE_DIR}/cmake/coverage_report.cmake
    COMMENT "Reporting line, function, and branch coverage"
    VERBATIM
)
add_custom_target(ares-complexity
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/complexity.cmake
    COMMENT "Measuring cyclomatic complexity"
    VERBATIM
)
add_custom_target(ares-assurance
    COMMAND ${CMAKE_COMMAND} -E echo "Running format, tidy, and complexity. Coverage is a separate instrumented build."
    DEPENDS ares-format-check ares-tidy ares-complexity
    VERBATIM
)

message(STATUS "ARES ${PROJECT_VERSION} sanitizers=${ARES_ENABLE_SANITIZERS} coverage=${ARES_ENABLE_COVERAGE}")
