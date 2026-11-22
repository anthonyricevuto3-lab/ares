# Verification Report

The post-release assurance assessment was performed against the frozen v1.0.0 baseline. This report records what was measured. It is not an independent test organization, and it is not NASA IV&V.

## Baseline

Annotated tag `v1.0.0` at `fcb6eda24fbeaf5e42c0f403ba977af1e427c944`. Date of the assurance measurements: 25 September 2026. Host: Windows 10, MSYS2 UCRT64, GNU 16.2.0, CMake 4.4.3, Ninja, Python 3.11.9. GoogleTest 1.15.2 is the version fetched by CMake.

The assurance measurements added tests. They did not change FDIR thresholds, recovery policy, modes, capacities, scenario timing, or the recording format. `ARES_ENABLE_COVERAGE` defaults off.

## Test results

| Run | Result |
| --- | --- |
| Debug ctest, UCRT64 GCC 16.2.0, Ninja, `build/debug` | 312 passed, 0 failed |
| Release ctest, same compiler, `build/release` | 312 passed, 0 failed |
| Coverage-instrumented ctest, `build/coverage` | 312 passed, 0 failed |

312 ctest cases: 311 GoogleTest (247 unit, 64 integration) plus `ares_smoke`. The smoke test also carries the integration label, so that label count is 65. The repeated 100-iteration campaign was not re-run for the assurance pass.

37 requirements have verification evidence. `ARES-REQ-REPLAY-002` is code inspection only. The other 36 cite a test for the behavioral clause. Twelve modeled hazards have verification evidence. No product defect was filed.

## Coverage

gcov 16.2.0 and gcovr 8.3. Build `build/coverage` with `-DARES_ENABLE_COVERAGE=ON`, run ctest, then `cmake --build build/coverage --target ares-coverage`. GCC bug 68080 can emit negative hit counts. The report uses gcovr `negative_hits.warn_once_per_file`. This is not MC/DC. No coverage percentage is a pass/fail gate. `--fail-under-line 0.1` only rejects an empty report.

| Set | Lines | Functions | Branches |
| --- | --- | --- | --- |
| `src/` and `include/ares/` | 85.7% (2979/3475) | 85.7% (667/778) | 73.8% (2053/2780) |
| Flight-side allow-list | 86.6% (2295/2651) | 84.2% (570/677) | 76.1% (1558/2047) |

The flight-side allow-list is `src/core/`, `src/flight/`, `src/simulation/`, `src/application.cpp`, `src/main.cpp`, and the matching `include/ares/` trees for core, flight, and simulation, plus `application.hpp` and `launch_options.hpp`. It excludes `src/recorder/` and `include/ares/recorder/`, including `replay.cpp`, `replay_main.cpp`, and `flight_recorder.hpp`.

| File | Line coverage | Classification |
| --- | --- | --- |
| `src/recorder/replay_main.cpp` | 0% (0/54) | Tests call the library. `replay.cpp` is 84.6% lines. `ARES-REQ-REPLAY-002` is inspection, not a claim that `main` is covered. |
| `src/main.cpp` | 33.3% (2/6) | Some `main` branches are untested. The suite calls `ares::run`. |
| `src/application.cpp` | 65.6% (198/302) | Some operator and host-clock branches. Named recovery requirements are tested through campaigns. |
| `include/ares/application.hpp` | 63.6% (42/66) | Defensive exit-code branches. |
| `include/ares/flight/fault.hpp` | 62.0% lines | Defensive `to_string` arms. |
| `include/ares/core/time.hpp` | 80.5% lines | Defensive overflow directions beyond the checked-time tests. |

## Complexity

lizard 1.17.10, `cmake --build build/debug --target ares-complexity`. Scope: `include/ares` and `src`, tests excluded. Summary: 6294 NLOC, average CCN 4.0, 332 functions, 8 above the voluntary threshold of 15. This is not SWE-220. The eight were left unsplit because a split would risk frozen behavior.

| Function | File | CCN |
| --- | --- | --- |
| `ares::run` | `src/application.cpp` | 56 |
| `ares::recorder::validate` | `src/recorder/replay.cpp` | 42 |
| `ares::recorder::replay_bytes` | `src/recorder/replay.cpp` | 30 |
| `ares::parse_arguments` | `include/ares/launch_options.hpp` | 27 |
| `ares::recorder::describe` | `src/recorder/replay.cpp` | 26 |
| `TaskSupervisor::thread_main` | `src/core/task_supervisor.cpp` | 25 |
| `RecoveryManager::observe_navigation` | `include/ares/flight/recovery.hpp` | 19 |
| `main` | `src/recorder/replay_main.cpp` | 17 |

The lizard numbers above were measured before the flight-task sources were renamed. That rename does not change control flow. Re-measure if a later change edits function bodies.

## Static analysis

| Check | Result |
| --- | --- |
| `ares-format-check` | Exit 0 |
| `ares-tidy` | Exit 0. 203325 warnings suppressed (203323 non-user, 2 NOLINT). No project diagnostic. |
| Compiler warnings | None. ARES targets use `-Werror`. Debug, Release, and coverage builds finished. |

## Dynamic analysis

AddressSanitizer and UndefinedBehaviorSanitizer were not re-run for the assurance assessment. The last green run is the GitHub Actions job on `fcb6eda`, the `v1.0.0` commit. That is prior evidence for those flight sources, not a new run. ThreadSanitizer was not run. It is unsupported in the current WSL memory mapping.

## Tooling

NASA tool accreditation was not performed.

| Tool | Version / environment | Purpose | Limitation |
| --- | --- | --- | --- |
| GCC | 16.2.0, MSYS2 UCRT64 | Windows compile and test | `__int128` excludes MSVC. ASan/UBSan libraries are absent. |
| Clang | Linux CI, and local tidy | Second compiler, ASan/UBSan, tidy | Not the Windows release compiler. |
| CMake / Ninja | CMake 4.4.3, Ninja | Generate the build | A hand-edited cache can diverge from the preset. |
| GoogleTest | 1.15.2, URL and hash | Test harness | A harness bug could hide a failure. |
| clang-format | repo `.clang-format` | Layout check | Does not judge correctness. |
| clang-tidy | repo `.clang-tidy` | Defect and standard checks | Does not measure coverage, complexity, or MC/DC. |
| ASan / UBSan | Linux Clang preset | Memory and undefined behavior | Instrumented binary only. Not a proof of absence. Not linked on UCRT64 GCC. |
| gcovr | 8.3, with gcov 16.2.0 | Line, function, and branch coverage | Not MC/DC. Negative-hit workaround for GCC bug 68080. |
| lizard | 1.17.10 | Cyclomatic complexity | Counts decision points. Not a proof of testability. |
| GitHub Actions | `.github/workflows/ci.yml` | Linux Debug, Release, ASan/UBSan, and Windows Debug | The hosted image is not accredited. |

## Known gaps

- No Technical Authority, so the NASA matrix is not an approved NASA matrix.
- IV&V was not performed. No human Formal Inspection was held.
- MC/DC was not measured.
- Linux Debug, Release, and ASan on the assurance tree were not repeated here. The `fcb6eda` CI job remains the Linux ASan evidence for the frozen flight sources.
- ThreadSanitizer is unavailable in the current WSL environment.
- `replay_main.cpp` is uncovered because tests call the library.
- Eight functions remain above the voluntary complexity threshold on purpose.

## Conclusion

Debug, Release, and coverage-instrumented test runs completed without a failure. Coverage and complexity were measured and the large gaps were classified. ARES 1.0.0 demonstrates documented alignment with selected NASA software engineering and software assurance practices.
