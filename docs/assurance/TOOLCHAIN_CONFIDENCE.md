# Toolchain confidence

NPR 7150.2D SWE-136 says the project manager shall validate and accredit the tools required to develop or maintain software, and the note defines accreditation as certification that a tool is acceptable for a specific purpose, conferred by the organization best positioned to make that judgment.

**NASA tool accreditation not performed.**

The table records what each tool is used for, how its result is cross-checked, whether it can change the executable, and the limitation that matters. Versions below are the ones observed on the Windows UCRT64 assurance host unless a metric row says otherwise. Linux CI uses the image in `.github/workflows/ci.yml`.

| Tool | Purpose | Cross-check | Affects the executable | Limitation |
| --- | --- | --- | --- | --- |
| GCC (UCRT64) | Compile and link the Windows binary | Same tests on Clang in Linux CI; warnings are errors | Yes | `__int128` ties the code to GCC or Clang. MSVC is rejected. |
| Clang (Linux CI and local WSL) | Second compiler, ASan/UBSan, tidy | GCC test suite | Yes, for the Linux build | Not the Windows release compiler. |
| ld / the compiler driver | Link | Tests launch the linked binary | Yes | No separate linker qualification. |
| CMake 3.20+ and Ninja or Unix Makefiles | Generate the build | Presets are in the repo; CI uses them | Indirectly, via flags | A hand-edited cache can diverge from the preset. |
| GoogleTest 1.15.2 | Test harness, fetched by URL and hash | Assertions check flight results, not the harness | No | A harness bug could hide a failure. Campaign determinism is the overlapping check. |
| clang-format | Layout | `ares-format-check` is diff-only | No | Does not judge correctness. |
| clang-tidy | Defects and coding-standard checks | Compiler warnings and tests | No | Does not measure coverage, complexity, or MC/DC. Suppressed hits in non-project code are not ARES defects. |
| AddressSanitizer and UndefinedBehaviorSanitizer | Memory and undefined-behavior symptoms at run time | The same tests without sanitizers | The instrumented binary only | Not linked on UCRT64 GCC. Not a proof of absence. |
| ThreadSanitizer | Data races | None on this host | Would affect an instrumented binary | Unsupported in the current WSL mapping. Not claimed. |
| gcov plus gcovr 8.3 | Line, function, and branch coverage | The counters come from the test run | The coverage binary only | Not MC/DC. GCC 16 gcov can emit negative hit counts (GCC bug 68080). The report uses gcovr's `negative_hits.warn_once_per_file` so those lines are kept with a warning instead of aborting the report. |
| lizard 1.17.10 | Cyclomatic complexity | Review of functions above 15 | No | Counts decision points. It is not a proof of testability. |
| GitHub Actions | Repeat Debug, Release, ASan, format, and tidy | Local UCRT64 Debug and Release | No | The hosted image is not accredited. |

Overlapping evidence for the flight binary is compiler warnings, the GoogleTest suite, ASan/UBSan on Linux, and ManualClock replay determinism. Agreement among those checks raises confidence. It is not accreditation.
