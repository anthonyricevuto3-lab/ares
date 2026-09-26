# Assurance report

ARES 1.0.0 demonstrates documented alignment with selected NASA software engineering and software assurance practices. It is not NASA compliant.

## Baseline

Annotated tag `v1.0.0` at `fcb6eda24fbeaf5e42c0f403ba977af1e427c944`. Work is on `assurance/nasa-alignment`. The tag was not moved. `git diff v1.0.0 -- include src` is empty. `tests/` differs by the evidence cases added in this pass.

## Standards

Read from the public NASA PDFs, not from a handbook summary:

- NPR 7150.2D, effective 8 March 2022, NODIS `N_PR_7150_002D_.pdf`
- NASA-STD-8739.8B, 8 September 2022
- NASA-STD-8739.9 with Change 1 (17 June 2013, change 7 October 2016). The NASA standards catalog lists this document inactive. It was used as inspection guidance only.

The three PDFs were not stored in the repository. They were read from the public NASA hosts for this review.

## Scope

Assurance artifacts, a C++ coding standard that matches existing practice, and reproducible coverage and complexity targets. No spacecraft feature. No threshold, mode, scenario, capacity, or recording-format change.

## Alignment

`NASA_ALIGNMENT_MATRIX.md` records each requested SWE with a project status. Institutional items that need a NASA Center, a contract, or a Technical Authority are marked Institutional / Not Applicable to Independent Project. SWE-219, SWE-220, SASS-01, IV&V, and the SFI requirements are explicitly not claimed.

## Traceability

37 ARES requirements in `REQUIREMENTS.md`. All 37 map to a design element, an implementation file, and verification evidence in `REQUIREMENTS_TRACEABILITY.md`. A behavioral clause cites a test that asserts it. An architectural clause is marked Inspection. `ARES-REQ-REPLAY-002` is inspection only. None maps to an open defect. Structural coverage is a separate measurement and is not 100 percent.

## Hazards

`HAZARD_ANALYSIS.md` accepts 12 modeled simulator conditions, ARES-HZ-001 through ARES-HZ-012. Each has a control. Runtime behavior is cited as a test. Architectural boundaries are cited as code inspection. This is an exercise. It is not an approved vehicle hazard analysis and it does not classify ARES as safety-critical.

## Measurements

| Topic | Result |
| --- | --- |
| Debug and Release tests | 312/312 each |
| Coverage tests | 312/312, then gcovr |
| Line / function / branch | 85.7% (2979/3475) / 85.7% (667/778) / 73.8% (2053/2780) product; 86.6% (2295/2651) / 84.2% (570/677) / 76.1% (1558/2047) flight-side allow-list |
| Complexity | 332 functions, average CCN 4.0, 8 reviewed above 15, none rewritten |
| clang-format | Clean |
| clang-tidy | Clean for project code |
| ASan/UBSan | Not re-run |
| TSan | Not run |

## Configuration management

`v1.0.0` remains the frozen baseline. This branch adds documentation and build-system targets. The coverage option defaults off, so a normal Debug or Release configure does not instrument the binary.

## Defects

No Critical, High, Medium, or Low product defect was opened. The eight complex functions were reviewed and left unchanged because a split would risk frozen behavior.

## Inspection and IV&V

Inspection plan, checklist, and blank report template are in place for a future human team. The review that produced this branch is an AI-assisted assurance review and a NASA-STD-8739.9-inspired source code review. It is not a NASA Formal Inspection. **IV&V NOT PERFORMED.**

## Acceptance criteria for this branch

| Criterion | Met |
| --- | --- |
| Each ARES requirement has an implementation owner | Yes |
| Each requirement has verification evidence | Yes |
| No open Critical or High assurance defect | Yes |
| Debug and Release suites pass | Yes |
| Coverage measured and the large gaps classified | Yes |
| Complexity measured and functions above 15 reviewed | Yes |
| clang-format clean | Yes |
| clang-tidy clean | Yes |
| ASan/UBSan clean on this branch | No. Last clean run is the v1.0.0 CI job, not this branch. |
| Traceability has no broken link for the flight and recovery requirements in the set | Yes |
| Hazard controls have verification evidence | Yes |
| Configuration baseline recorded | Yes |
| Known limitations documented | Yes |
| NASA approval | Not a criterion |

## Known gaps

- No Technical Authority, so the mapping matrix is not an approved NASA matrix.
- NASA-STD-8739.9 is inactive, and no human inspection team has used the plan.
- IV&V was not performed.
- MC/DC was not measured.
- Linux Debug, Release, and ASan were not repeated here.
- ThreadSanitizer is unavailable in the current WSL environment.
- `replay_main.cpp` is uncovered because tests call the library.
- `ares::run` and `RecoveryManager::observe_navigation` remain above the voluntary complexity threshold on purpose.
- GCC 16 gcov needed gcovr's negative-hit workaround.

## Future work

A human inspection against the checklist, a Linux ASan rerun on this branch if the CMake option set is what must be shown, and a split of `observe_navigation` only when recovery code is already being changed.

## Conclusion

The evidence corrections are in the working tree and are not yet committed. `v1.0.0` was not moved.

ARES 1.0.0 demonstrates documented alignment with selected NASA software engineering and software assurance practices.
