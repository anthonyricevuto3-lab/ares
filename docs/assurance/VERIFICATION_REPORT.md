# Verification report

Branch `assurance/nasa-alignment`, parent `fcb6eda` (`v1.0.0`). Date of the runs: 25 September 2026. Evaluator: the assurance pass on this branch. This is not an independent test organization.

No file under `include/` or `src/` differs from `v1.0.0`. `tests/` adds two cases and tightens the recorder-open assertion. Flight behavior was not modified. Debug and Release binaries used for the suites below were rebuilt with coverage and sanitizers off.

## Results

| Run | Result |
| --- | --- |
| Debug ctest, UCRT64 GCC 16.2.0, Ninja, `build/debug` | 312 passed, 0 failed |
| Release ctest, same compiler, `build/release` | 312 passed, 0 failed |
| Coverage-instrumented ctest, `build/coverage` | 312 passed, 0 failed |
| `ares-format-check` | Exit 0 |
| `ares-tidy` | Exit 0. Suppressed warnings are outside project headers, plus 2 NOLINT. No ARES diagnostic was reported. |
| gcovr 8.3 line / function / branch | 85.7% (2979/3475) / 85.7% (667/778) / 73.8% (2053/2780) on `src` and `include/ares`. Flight-side allow-list 86.6% (2295/2651) / 84.2% (570/677) / 76.1% (1558/2047). See `METRICS.md`. |
| lizard 1.17.10 | 332 functions, average CCN 4.0, 8 functions above 15. Measured before this evidence pass. `include/` and `src/` are unchanged, so it was not re-run. Each function is reviewed in `METRICS.md`. No refactor. |
| ASan/UBSan | Not run on this branch. |
| TSan | Not run. |
| Repeated 100-iteration campaign | Not run. Production code did not change. |

## Evaluation

The suites that ran completed without a failure. Coverage was produced by executing the instrumented tests, which is the measurement SWE-190 describes, applied voluntarily. The empty-report failure path was checked earlier in the same pass: a filter that matched nothing is now rejected by `--fail-under-line 0.1`, and an uninstrumented build still fails `ares-coverage` because `ARES_ENABLE_COVERAGE` is off.

Uncovered replay `main` and a thin `src/main.cpp` are wrapper gaps. They do not leave a named behavioral requirement without a test. `ARES-REQ-REPLAY-002` is code inspection of the replay executable, not a claim that `replay_main.cpp` is covered. The other requirements name a test in the 312.

## Not claimed

Linux Debug, Linux Release, and Linux ASan were not repeated on this branch. They passed on GitHub Actions for commit `fcb6eda` before the tag. That is prior evidence for the same flight sources, not a new run. ThreadSanitizer is not claimed.
