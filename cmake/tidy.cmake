if(NOT CLANG_TIDY OR CLANG_TIDY STREQUAL "CLANG_TIDY-NOTFOUND")
    message(FATAL_ERROR "clang-tidy was not found. Install it and reconfigure.")
endif()
if(NOT EXISTS "${BUILD_DIR}/compile_commands.json")
    message(FATAL_ERROR "compile_commands.json is missing in ${BUILD_DIR}. Configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON.")
endif()

file(GLOB_RECURSE ARES_TIDY_SOURCES "${SOURCE_DIR}/src/*.cpp")
if(NOT ARES_TIDY_SOURCES)
    message(FATAL_ERROR "No production translation units were found.")
endif()

execute_process(
    COMMAND "${CLANG_TIDY}" -p "${BUILD_DIR}" --warnings-as-errors=* ${ARES_TIDY_SOURCES}
    RESULT_VARIABLE _ares_tidy_status
    WORKING_DIRECTORY "${SOURCE_DIR}"
)
if(NOT _ares_tidy_status EQUAL 0)
    message(FATAL_ERROR "clang-tidy failed with status ${_ares_tidy_status}")
endif()
