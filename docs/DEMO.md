# ARES v1.0 demonstration

A reviewer can walk through this in about ten minutes after a release build. The four demos use the frozen flight software. They do not add a fault family or a recovery action.

## Prerequisites

CMake 3.20 or newer, Ninja, and GCC or Clang with C++20 and `__int128`. On Windows, use an MSYS2 UCRT64 shell. MSVC is not supported. Commands below assume the repository root.

## Build

```sh
cmake --preset release
cmake --build --preset release
```

Windows binaries are `build/release/ares.exe` and `build/release/ares-replay.exe`. One command for all four demos:

```sh
sh scripts/demo_v1.sh
```

```powershell
.\scripts\demo_v1.ps1
```

Set `ARES` and `REPLAY` to installed binaries if you are not using `build/release`. Recordings are written under `build/` unless `RECORD_DIR` is set. They are not part of the repository.

The `ares` process uses the host steady clock. The schedules are deterministic in ARES time. A loaded machine can add deadline misses. The manual-clock campaign tests are the oracle.

## Demo 1: Nominal

```sh
./build/release/ares --scenario nominal --seed 1 --duration-ms 1000
```

Proves a clean boot and a quiet mission. Look for `Boot -> Initialization -> Standby -> Nominal`, `final_mode: Nominal`, `active_gps: primary`, `navigation_generation: 0`, no recovery, and `exit: Success`.

## Demo 2: GPS autonomous recovery

```sh
./build/release/ares --scenario gps_stale --seed 42 --duration-ms 12000 --record build/demo-gps.bin
./build/release/ares-replay --verify build/demo-gps.bin
```

The primary GPS freezes at 5 seconds for 4 seconds. Freshness turns that into a persistent warning. The spacecraft enters Degraded, isolates the primary, selects the backup, and verifies three usable backup samples. It returns to Nominal and stays on the backup. There is no automatic failback.

Proves detection, isolation, redundancy, and verified recovery. Expect process exit 0 and `valid` from replay. The timeline should contain a fault activation, `RecoveryStarted`, the backup selection, and `RecoverySucceeded`.

## Demo 3: Navigation autonomous restart

```sh
./build/release/ares --scenario nav_restart --seed 1 --duration-ms 8000 --record build/demo-nav.bin
./build/release/ares-replay --verify build/demo-nav.bin
```

A 150 ms navigation delay lasts 1 second. When the host completes that window, deadline misses escalate through Advisory and Warning to Critical, then SafeMode. The same worker restarts navigation. Generation moves from 0 to 1. After the delay ends, three on-time completions verify the restart, the deadline fault clears, and SafeMode returns to Standby.

`nav_restart` uses the host steady clock. That 1-second window has produced both the expected generation-1 verified restart and a normal generation-0 completion on some host-scheduled runs. Process exit 0 and a valid replay are not enough. `scripts/demo_nav_restart.sh` and `scripts/demo_nav_restart.ps1` succeed only when the process exit is 0, the summary lines are exactly `navigation_generation: 1`, `recovery_successes: 1`, and `recovery_failures: 0`, and `ares-replay --verify` exits 0. Generation 0 or `recovery_successes: 0` fails the wrapper with "Navigation restart was not observed on this host/run." That is a failed demonstration run because of host scheduling, not a flight-software failure and not evidence that ARES crashed or violated the recovery design. The wrapper does not retry and does not lengthen the run. The host demo is not perfectly deterministic.

## Demo 4: Bounded recovery failure

```sh
./build/release/ares --scenario restart_fail --seed 1 --duration-ms 6000
```

The same delay is held for 30 seconds, longer than this run. Both restart attempts fail verification. There is no third attempt. The final mode stays SafeMode.

6000 ms is the canonical tested duration used for the v1.0 release demo on the validation host. It is not a mathematical guarantee. 5000 ms did not always reach `RecoveryFailed`. 5500 ms did reach it in several probes. 6000 ms was chosen to provide additional margin. On that host the summary showed `recovery_failures: 1`, `navigation_generation: 2`, SafeMode, and about 27 of 32 miss-log entries. The process exit was Success. Recovery failure does not by itself change the process exit code.

Host steady-clock scheduling can change how many cycles complete. On another host, 6000 ms fails the demo check if `RecoveryFailed` has not occurred yet. A sufficiently long run can instead fill the 32-entry miss history after `RecoveryFailed` and exit `FaultHistoryOverflow` (9). That code is the bounded history. The recovery cap is still two attempts. `scripts/demo_v1` and `scripts/demo_recovery_failure` accept process exit 0 or 9 only when the summary line is `recovery_failures: 1`. The wrapper then exits 0 and prints which process result occurred. A missing summary or any other process exit fails the wrapper.

## What each demo proves

| Demo | Story |
| --- | --- |
| Nominal | Boot, three tasks, primary GPS, no recovery |
| GPS | Chaos condition, freshness, Degraded, backup, verification, Nominal |
| Navigation | Deadline escalation, SafeMode, restart, generation, Standby |
| Failure | Two attempts, then stop. No infinite restart |

## Known caveats

Host-clock recordings of the same scenario are not byte-identical. Manual-clock campaigns are. Replay checks the bytes it can read. It does not run the tasks again. Communications dropout, GPS failback, and checkpoint rollback are not in this demonstration.
