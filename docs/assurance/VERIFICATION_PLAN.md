# Verification plan

This plan is the project's stand-in for the test plan and procedures in NPR 7150.2D SWE-065. The procedures are the GoogleTest cases and the demo scripts. Results are recorded in `VERIFICATION_REPORT.md`.

## Unit testing

`tests/unit/` exercises clocks, periodic tasks, the supervisor, mode machine, fault registry, detection, policy, mailbox, recovery, GPS selection, sensors, chaos, the recorder, and replay. SWE-062 asks that the code be unit tested. SWE-186 asks that unit results be repeatable. Timing tests advance `ManualClock`. Noise tests pass an explicit seed.

## Integration testing

`tests/integration/` boots the application, runs scenarios, and checks exit codes, mode traces, and recordings. These are the end-to-end procedures for the requirements in `REQUIREMENTS.md`.

## Fault-injection campaigns

`ChaosCampaign.*` and `SensorInjection.*` apply the frozen scenarios. They are requirements-based tests for FDIR and recovery (the intent of SWE-066), not a coverage-percentage exercise.

## Regression

SWE-191 asks for regression testing so previously integrated behavior is not broken. The regression set is the full ctest suite on the Debug and Release presets, plus the existing GitHub Actions jobs: Linux Debug (format and tidy), Linux Release, Linux ASan/UBSan, and Windows UCRT64 Debug. This branch does not weaken those tests and does not add a coverage percentage gate.

## Static analysis

`ares-tidy` runs clang-tidy. `ares-format-check` runs clang-format. Compiler warnings are errors on ARES targets. SWE-135 lists defects, software security, coverage, and complexity as reasons to use static analysis. clang-tidy is not a coverage tool and not a complexity tool. Those are measured separately. clang-tidy findings in generated or third-party code that the project suppresses are not ARES defects.

## Dynamic analysis

The `asan-ubsan` preset enables AddressSanitizer and UndefinedBehaviorSanitizer. MSYS2 UCRT64 GCC does not ship those libraries, so that preset is a Linux Clang run. ThreadSanitizer is not available in the current WSL environment. A TSan result is not claimed.

## Record and replay

`Recording.*` and `Replay.*` check format 1.0, CRC, sequence, and rejection of malformed files. Replay does not re-execute flight code.

## Coverage

Configure a separate build with `-DARES_ENABLE_COVERAGE=ON`, run ctest, then build the `ares-coverage` target. The target fails if coverage was not enabled or if `gcovr` cannot be imported. The pinned tool is gcovr 8.3. Metrics are line, function, and branch. MC/DC is not measured.

Uncovered flight code, when reviewed, is classified as one of: missing requirement, missing test, defensive or unreachable, platform-specific, or intentional and deactivated. That classification follows the note under SWE-189. It is not a claim that every gap has been closed.

## Complexity

Build the `ares-complexity` target. It fails if `lizard` cannot be imported. The pinned tool is lizard 1.17.10. The voluntary review threshold for flight-side functions is cyclomatic complexity 15. A function above 15 is reviewed. It is not a SWE-220 waiver.

## Release and demo validation

`scripts/demo_v1.ps1` and `scripts/demo_v1.sh` run the four canonical demos. Navigation and recovery-failure wrappers check summary lines, not only the process exit. Those scripts are operator checks. The repeatable evidence is the ManualClock campaign tests.

## What this plan does not do

It does not call the GoogleTest suite independent verification. The note under SWE-066 recommends an organization outside the development team for Class A, B, and C. ARES is not classified, and the tests were written with the code.
