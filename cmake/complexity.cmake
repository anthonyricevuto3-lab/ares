find_program(ARES_PYTHON NAMES python python3 REQUIRED)
execute_process(
    COMMAND ${ARES_PYTHON} -c "import lizard"
    RESULT_VARIABLE lizard_rc
    ERROR_VARIABLE lizard_err
)
if(NOT lizard_rc EQUAL 0)
    message(FATAL_ERROR
        "lizard is required for ares-complexity and was not importable. "
        "Install with: python -m pip install lizard==1.17.10\n${lizard_err}")
endif()

set(report "${BUILD_DIR}/complexity.txt")
execute_process(
    COMMAND ${ARES_PYTHON} -m lizard
        --CCN 15
        -l cpp
        -x "*/tests/*"
        ${SOURCE_DIR}/include/ares
        ${SOURCE_DIR}/src
    RESULT_VARIABLE lizard_run
    OUTPUT_FILE ${report}
    ERROR_VARIABLE lizard_run_err
)
# lizard exits 1 when any function exceeds the warning threshold. That is a
# measurement result, not a missing tool. Keep the report either way.
if(NOT lizard_run EQUAL 0 AND NOT lizard_run EQUAL 1)
    message(FATAL_ERROR "lizard failed: ${lizard_run_err}")
endif()
message(STATUS "Complexity report written to ${report} (lizard status ${lizard_run})")
