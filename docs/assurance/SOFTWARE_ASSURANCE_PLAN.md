# ARES Software Assurance

ARES applies selected NASA software engineering and software assurance practices with documented project-specific tailoring. This is an independent educational alignment exercise. It is not NASA certification, NASA approval, flight qualification, DO-178C compliance, formal IV&V, or a completed NASA Software Formal Inspection.

The practices considered are NPR 7150.2D (effective 8 March 2022), NASA-STD-8739.8B (8 September 2022), and NASA-STD-8739.9 with Change 1. The standards catalog lists NASA-STD-8739.9 inactive. It was used only as inspection guidance. The mapping is `NASA_ALIGNMENT_MATRIX.md`. There is no NASA Technical Authority signature, so that matrix is a project record, not an approved NASA requirements mapping matrix.

## Purpose and scope

This plan says which assurance practices ARES uses. Measured results are only in `VERIFICATION_REPORT.md`.

The post-release assurance assessment was performed against the frozen v1.0.0 baseline. It did not add a spacecraft feature, a fault family, a threshold change, or a recording-format change. ARES 1.0.0 does not control a real spacecraft. NASA safety-critical requirements, including SWE-219 MC/DC and SWE-220 cyclomatic compliance, are not formally invoked. Complexity 15 is a voluntary review threshold. Using that number is not a compliance claim.

**IV&V NOT PERFORMED.** An author review, including an AI-assisted review, is not verification by an organization that is technically, managerially, and financially independent of development.

## Baseline and applicability

The frozen product baseline is annotated tag `v1.0.0` at `fcb6eda24fbeaf5e42c0f403ba977af1e427c944`. Release tags are immutable. Git history is not rewritten to move a tag.

NPR 7150.2D and NASA-STD-8739.8B apply to NASA work to the extent a contract or Center process says so. ARES has no NASA contract, Center, Safety and Mission Assurance organization, or Technical Authority. Requirements that need those organizations are Institutional / Not Applicable to Independent Project. Selected engineering practices are Voluntarily Adopted or Partially Implemented.

## Requirements and traceability

Software requirements are ARES-owned statements in `REQUIREMENTS.md`. They describe the simulator that exists. They are not copies of SWE or SASS statements. `REQUIREMENTS_TRACEABILITY.md` maps each requirement to design, code, verification, and a hazard when one applies. A behavioral clause cites a test that asserts it. An architectural clause is marked Inspection.

## Architecture and implementation

`docs/ARCHITECTURE.md` is the current-state architecture. Headers under `include/ares/` and the unit tests name the lower-level units. Implementation rules are `CODING_STANDARD.md`, `.clang-format`, `.clang-tidy`, and warnings as errors on ARES targets.

## Coding standard

`CODING_STANDARD.md` is the C++ rule set: explicit-width integers, no dumped C++ structs in the recording, checked arithmetic, joined threads, and the simulation/flight boundary. Format and tidy enforce layout and a subset of those rules. They do not measure coverage or complexity.

## Verification strategy

Results are recorded only in `VERIFICATION_REPORT.md`. The strategy is:

- Unit tests under `tests/unit/` for clocks, tasks, the supervisor, modes, faults, recovery, sensors, and replay. Timing tests advance `ManualClock`. Noise tests pass an explicit seed.
- Integration tests under `tests/integration/` for boot, exit codes, campaigns, and recordings.
- Fault-injection campaigns (`ChaosCampaign.*` and `SensorInjection.*`) for the frozen scenarios.
- Regression is the full Debug and Release ctest suites plus the GitHub Actions jobs: Linux Debug with format and tidy, Linux Release, Linux ASan/UBSan, and Windows UCRT64 Debug.
- Static analysis is clang-tidy, clang-format, and compiler warnings as errors. clang-tidy is not a coverage or complexity tool.
- Dynamic analysis is AddressSanitizer and UndefinedBehaviorSanitizer on the Linux Clang preset. ThreadSanitizer is not available in the current WSL mapping and is not claimed.
- Record and replay tests check format 1.0. Replay does not re-execute flight code.
- Coverage is a separate `-DARES_ENABLE_COVERAGE=ON` build, then `ares-coverage` (gcovr line, function, and branch). It is not MC/DC and not a percentage gate.
- Complexity is `ares-complexity` (lizard). Functions above 15 are reviewed. They are not treated as a SWE-220 waiver.
- `scripts/demo_v1.sh` and `scripts/demo_v1.ps1` are operator checks. ManualClock campaigns are the repeatable evidence.

The GoogleTest suite was written with the code. It is not an independent test organization.

## Configuration management

Git is the configuration-control system. Controlled items are source under `include/ares/`, `src/`, and `tests/`; `CMakeLists.txt`, `CMakePresets.json`, and `cmake/`; `.github/workflows/ci.yml`; `.clang-format` and `.clang-tidy`; `scripts/demo_v1.sh`, `scripts/demo_v1.ps1`, `scripts/measure_coverage.ps1`, and `scripts/measure_complexity.ps1`; requirements, architecture notes, and `docs/assurance/`; scenario schedules; and the recorder layout in `include/ares/recorder/format.hpp`.

The application version is `project(ARES VERSION 1.0.0)`. The recording format version is independent and remains 1.0. `docs/RELEASE_NOTES.md` describes the tags. CPack builds TGZ and ZIP archives from a tag.

Build directories, recordings, and local packages are not controlled. `.gitignore` excludes them.

A change to flight thresholds, mode transitions, scenario timing, capacities, or the recording format needs a justification, review, and tests. There is no NASA change-control board. The maintainer authorizes changes. Configuration status is `git status`, `git describe`, and `git tag --list`.

## Risk and defect handling

Remaining engineering risks are in `RISK_REGISTER.md`. Likelihood and impact are qualitative.

ARES severities are Critical, High, Medium, and Low. They are not NASA formal-inspection classifications, and they are not the SWE-202 loss-of-life classes. ARES does not fly.

A Critical or High fix requires a root-cause note, the correction, a regression test, and a recorded verification run. Update the traceability row when a requirement was wrong. No historical defect database is kept. No Critical or High product defect is open against the frozen flight code.

## Hazard analysis

`HAZARD_ANALYSIS.md` models twelve simulator conditions and their controls. Effects are wrong mode, lost diagnostic evidence, or a stopped mission inside the simulation. The exercise does not classify ARES as safety-critical and does not state loss of crew or loss of a vehicle.

## Peer-review status

A human NASA Formal Inspection has not been performed. NASA-STD-8739.9 calls for a trained moderator and a team of at least three. That team was not convened. An AI-assisted review is not a NASA Formal Inspection.

A future peer review can use `REQUIREMENTS.md`, `CODING_STANDARD.md`, the tests, and `REQUIREMENTS_TRACEABILITY.md` as inputs. `CONTRIBUTING.md` lists the engineering checks for an ordinary change. No inspection measurements exist, because no formal inspection was held.

## Metrics and release gates

`VERIFICATION_REPORT.md` is the record of test counts, coverage, complexity, format, tidy, and sanitizers. NASA approval is not a release gate. A normal Debug or Release configure leaves coverage and sanitizers off.

## Claims not made

NASA tool accreditation was not performed. SWE-136 is Not Implemented. Agreement among the compiler, the test suite, Linux ASan/UBSan, and ManualClock campaigns raises confidence. It is not accreditation.

Also not claimed: official safety-critical classification, SWE-219, SWE-220 compliance, NASA Technical Authority approval, NASA IV&V, and a completed NASA Software Formal Inspection.
