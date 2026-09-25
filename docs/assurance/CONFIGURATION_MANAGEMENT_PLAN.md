# Configuration management plan

This plan addresses the intent of NPR 7150.2D SWE-079 through SWE-085 for an independent repository. It is not a NASA Center configuration-management process.

## Baseline

The frozen product baseline is annotated tag `v1.0.0` at `fcb6eda24fbeaf5e42c0f403ba977af1e427c944`. This branch `assurance/nasa-alignment` starts from that commit. The tag is not moved and history is not rewritten.

## Controlled items

- Source under `include/ares/`, `src/`, and `tests/`
- `CMakeLists.txt`, `CMakePresets.json`, and `cmake/`
- Compiler expectation: C++20, GCC or Clang with `__int128`
- `.github/workflows/ci.yml`
- `.clang-format` and `.clang-tidy`
- `scripts/measure_coverage.ps1` and `scripts/measure_complexity.ps1`
- Requirements, architecture notes, and `docs/assurance/`
- Scenario schedules in `include/ares/simulation/scenarios.hpp`
- Binary recorder layout in `include/ares/recorder/format.hpp` (format 1.0)
- Release tags and the CPack packages built from a tag

Build directories, recordings, and local packages are not controlled. `.gitignore` excludes them.

## Branching and review

`main` holds released history. Assurance work stays on `assurance/nasa-alignment` until the maintainer merges it. A change to flight thresholds, mode transitions, scenario timing, capacities, or the recording format needs a separate justification. This branch does not make those changes.

The maintainer is the authority for a merge. There is no NASA change control board (SWE-082 is only partly met).

## Versioning and release

`project(ARES VERSION 1.0.0)` is the application version. The recorder format version is independent and remains 1.0. Releases are annotated tags. `docs/RELEASE_NOTES.md` describes the tag. CPack produces TGZ and ZIP archives of the install tree.

## Status and audit

Configuration status is `git status`, `git describe`, and the tag list. An audit compares the work tree to `v1.0.0` and confirms flight sources are unchanged aside from any defect fix recorded in `DEFECT_MANAGEMENT.md`. The audit result for this branch is in `ASSURANCE_REPORT.md`.

## Storage and delivery

The remote is the GitHub repository already used for `v1.0.0`. Delivery of a NASA copy is not applicable (SWE-040, SWE-042).
