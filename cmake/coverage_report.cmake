if(NOT COVERAGE)
    message(FATAL_ERROR
        "ares-coverage refuses to report success on an uninstrumented build. "
        "Reconfigure with -DARES_ENABLE_COVERAGE=ON, build, run ctest, then "
        "build the ares-coverage target. Install gcovr 8.3 with: "
        "python -m pip install gcovr==8.3")
endif()

# Filters are regular expressions. The repository path contains parentheses,
# so match the relative directories instead of the absolute path.
find_program(ARES_PYTHON NAMES python python3 REQUIRED)
find_program(ARES_GCOV NAMES gcov REQUIRED)
execute_process(
    COMMAND ${ARES_PYTHON} -m gcovr --version
    RESULT_VARIABLE gcovr_rc
    OUTPUT_VARIABLE gcovr_out
    ERROR_VARIABLE gcovr_err
)
if(NOT gcovr_rc EQUAL 0)
    message(FATAL_ERROR
        "gcovr is required and was not importable. "
        "Install with: python -m pip install gcovr==8.3\n${gcovr_err}")
endif()

set(summary_txt "${BUILD_DIR}/coverage-summary.txt")
set(summary_json "${BUILD_DIR}/coverage-summary.json")
set(flight_json "${BUILD_DIR}/coverage-flight.json")
execute_process(
    COMMAND ${ARES_PYTHON} -m gcovr
        --root ${SOURCE_DIR}
        --object-directory ${BUILD_DIR}
        --gcov-executable ${ARES_GCOV}
        --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
        --filter src/
        --filter include/ares/
        --exclude-throw-branches
        --exclude-unreachable-branches
        --txt-metric branch
        --txt ${summary_txt}
        --json-summary ${summary_json}
        --json-summary-pretty
        --print-summary
        --fail-under-line 0.1
    RESULT_VARIABLE report_rc
    WORKING_DIRECTORY ${SOURCE_DIR}
)
if(NOT report_rc EQUAL 0)
    message(FATAL_ERROR
        "gcovr failed or reported no source lines (status ${report_rc}). "
        "A zero-line report is not a successful coverage run.")
endif()

execute_process(
    COMMAND ${ARES_PYTHON} -m gcovr
        --root ${SOURCE_DIR}
        --object-directory ${BUILD_DIR}
        --gcov-executable ${ARES_GCOV}
        --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
        --filter src/core/
        --filter src/flight/
        --filter src/simulation/
        --filter src/application.cpp
        --filter src/main.cpp
        --filter include/ares/core/
        --filter include/ares/flight/
        --filter include/ares/simulation/
        --filter include/ares/application.hpp
        --filter include/ares/launch_options.hpp
        --exclude-throw-branches
        --exclude-unreachable-branches
        --json-summary ${flight_json}
        --json-summary-pretty
        --print-summary
    RESULT_VARIABLE flight_rc
    WORKING_DIRECTORY ${SOURCE_DIR}
)
if(NOT flight_rc EQUAL 0)
    message(FATAL_ERROR "flight-side gcovr report failed with status ${flight_rc}")
endif()
message(STATUS "Coverage text: ${summary_txt}")
message(STATUS "Coverage JSON: ${summary_json}")
message(STATUS "Flight coverage JSON: ${flight_json}")
