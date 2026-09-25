if(NOT CLANG_FORMAT OR CLANG_FORMAT STREQUAL "CLANG_FORMAT-NOTFOUND")
    message(FATAL_ERROR "clang-format was not found. Install it and reconfigure.")
endif()

file(GLOB_RECURSE ARES_FORMAT_SOURCES
    "${SOURCE_DIR}/include/*.hpp"
    "${SOURCE_DIR}/src/*.hpp"
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/tests/*.hpp"
    "${SOURCE_DIR}/tests/*.cpp"
)
if(NOT ARES_FORMAT_SOURCES)
    message(FATAL_ERROR "No C++ sources were found to format.")
endif()

if(FIX)
    set(_ares_format_args -i)
else()
    set(_ares_format_args --dry-run --Werror)
endif()

execute_process(
    COMMAND "${CLANG_FORMAT}" ${_ares_format_args} ${ARES_FORMAT_SOURCES}
    RESULT_VARIABLE _ares_format_status
    WORKING_DIRECTORY "${SOURCE_DIR}"
)
if(NOT _ares_format_status EQUAL 0)
    message(FATAL_ERROR "clang-format failed with status ${_ares_format_status}")
endif()
