# Assurance metrics

Values below were measured on this branch. A cell is empty when the tool did not run. Nothing here is estimated.

Host: Windows 10, MSYS2 UCRT64, GNU 16.2.0, CMake 4.4.3, Ninja, Python 3.11.9. GoogleTest 1.15.2 is the version fetched by CMake.

## Tests

| Metric | Value | Command |
| --- | --- | --- |
| ctest count | 312 passed, 0 failed | `ctest --test-dir build/debug` and the same for `build/release` and `build/coverage` |
| GoogleTest cases | 311 | `TEST`, `TEST_F`, and `TEST_P` macros under `tests/` |
| Unit cases | 247 | macros under `tests/unit/` |
| Integration cases | 64 | macros under `tests/integration/` |
| Extra ctest | 1 | `ares_smoke`, not a GoogleTest macro. Its ctest labels are `smoke` and `integration`, so the integration label count is 65. |
| Repeated 100-run campaign | Not re-run | No production code changed, so the v1.0.0 campaign was not repeated |

## Coverage

Tool: gcov 16.2.0 and gcovr 8.3. Build: `build/coverage` with `-DARES_ENABLE_COVERAGE=ON`, then `ctest`, then `cmake --build build/coverage --target ares-coverage`. One negative-hit warning from GCC bug 68080 was ignored per file, as recorded in `TOOLCHAIN_CONFIDENCE.md`. This is not MC/DC.

| Set | Lines | Functions | Branches |
| --- | --- | --- | --- |
| `src/` and `include/ares/` (tests excluded) | 85.7% (2979/3475) | 85.7% (667/778) | 73.8% (2053/2780) |
| Flight-side allow-list: `src/core/`, `src/flight/`, `src/simulation/`, `src/application.cpp`, `src/main.cpp`, and the matching `include/ares/` trees for core, flight, simulation, plus `application.hpp` and `launch_options.hpp`. This excludes `src/recorder/` and `include/ares/recorder/`, including `replay.cpp`, `replay_main.cpp`, and `flight_recorder.hpp`. | 86.6% (2295/2651) | 84.2% (570/677) | 76.1% (1558/2047) |

Notable uncovered product files from `build/coverage/coverage-summary.json`:

| File | Line coverage | Classification |
| --- | --- | --- |
| `src/recorder/replay_main.cpp` | 0% (0/54) | Missing test of the replay executable entry. The library in `replay.cpp` is at 84.6% lines. |
| `src/main.cpp` | 33.3% (2/6) | Missing test of some `main` branches. `ares::run` is what the suite calls. |
| `src/application.cpp` | 65.6% (198/302) | Missing tests for some operator and host-clock branches. Named recovery requirements are tested through campaigns. |
| `include/ares/application.hpp` | 63.6% (42/66) | Defensive exit-code branches not taken by the suite. |
| `include/ares/flight/fault.hpp` | 62.0% lines, 46.9% branches | Defensive `to_string` arms. |
| `include/ares/core/time.hpp` | 80.5% lines, 52.3% branches | Defensive overflow directions beyond the checked-time tests. |

No coverage percentage is a pass/fail gate. `--fail-under-line 0.1` only rejects an empty report.

## Complexity

Tool: lizard 1.17.10, `cmake --build build/debug --target ares-complexity`. Scope: `include/ares` and `src`, tests excluded. Lizard's summary line: 6294 NLOC, average CCN 4.0, 332 functions, 8 warnings. The voluntary review threshold is 15. This is not SWE-220.

| Function | File | CCN | Review |
| --- | --- | --- | --- |
| `ares::run` | `src/application.cpp` | 56 | Mission setup and teardown. Splitting it would risk exit-code behavior. Left as is. |
| `ares::recorder::validate` | `src/recorder/replay.cpp` | 42 | One checker per recording failure. The branches are the tests. Left as is. |
| `ares::recorder::replay_bytes` | `src/recorder/replay.cpp` | 30 | Sequential record walk. Left as is. |
| `ares::parse_arguments` | `include/ares/launch_options.hpp` | 27 | CLI validation. Left as is. |
| `ares::recorder::describe` | `src/recorder/replay.cpp` | 26 | Text report. Left as is. |
| `TaskSupervisor::thread_main` | `src/core/task_supervisor.cpp` | 25 | Worker loop. A split is a behavior risk. Left as is. |
| `RecoveryManager::observe_navigation` | `include/ares/flight/recovery.hpp` | 19 | Recovery decisions. Worth a future split only when recovery is otherwise edited. Not split here. |
| `main` | `src/recorder/replay_main.cpp` | 17 | Replay CLI. Left as is. |

## Static and dynamic analysis

| Check | Result | Command |
| --- | --- | --- |
| clang-format | Pass, exit 0 | `cmake --build build/debug --target ares-format-check` |
| clang-tidy | Pass, exit 0. 203325 warnings suppressed (203323 non-user, 2 NOLINT). No project diagnostic printed. | `cmake --build build/debug --target ares-tidy` |
| Compiler warnings | None. ARES targets use `-Werror`. Debug, Release, and coverage builds finished. | the three `cmake --build` runs |
| ASan/UBSan | Not run on this branch | Last green run is GitHub Actions on `fcb6eda` (the `v1.0.0` commit). Not claimed as a new result. |
| TSan | Not run | Unsupported in the current WSL mapping. |

## Requirements and hazards

| Metric | Value | Source |
| --- | --- | --- |
| ARES requirements | 37 | `REQUIREMENTS.md` |
| Requirements with verification evidence | 37 | `REQUIREMENTS_TRACEABILITY.md` |
| Requirements whose only evidence is code inspection | 1 | `ARES-REQ-REPLAY-002`. The other 36 cite a test for the behavioral clause. |
| Requirements with no verification | 0 | same |
| Modeled hazards | 12 | `HAZARD_ANALYSIS.md` |
| Hazards with verification evidence | 12 | Behavioral controls cite tests. Architectural controls cite code inspection. |
| Open Critical / High / Medium / Low defects | 0 / 0 / 0 / 0 | No defect was filed. Process gaps are in the assurance report, not the defect log. |
| Closed defects in a database | None recorded | No historical database was created. |
