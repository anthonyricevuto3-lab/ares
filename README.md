# ARES

ARES (Autonomous Resilient Embedded Spacecraft System) is a C++20 flight-software simulation, version 0.6.0. It boots a spacecraft mode machine, runs three periodic tasks, flies a small digital spacecraft, detects a closed set of faults, and can recover in two ways: restart the navigation task inside its existing thread, or fail over from a primary GPS to a backup GPS. An optional flight recorder writes a versioned binary mission history. `ares-replay` reads that file offline.

This is not certified real-time flight software. Deadline checks on the operating-system clock describe desktop scheduling behavior. A recovery failure keeps the vehicle conservative. It does not by itself change the process exit code.

## What 0.6 adds

`--record FILE` asks the mission to keep a bounded binary history. The default is no file. The recorder watches events the flight software already publishes. It does not select a sensor, restart a task, or change a mode. A recording failure is exit code 10 when the flight itself succeeded. `ares-replay FILE` prints the timeline and a summary. `--verify` and `--summary` are the short forms. `docs/FLIGHT_RECORDER.md` and `docs/REPLAY.md` describe the format.

## What 0.5 does

Navigation and health publish observations into a fixed mailbox. The health task is the only writer of the fault registry and of recovery progress. The mode machine remains the only mode authority.

A critical navigation deadline miss can restart that task, at most twice in one episode. The same worker finishes the old generation before the next one starts. Success is three consecutive on-time completions from the new generation, not the restart itself. `SafeMode` does not return to `Standby` while that check is still open or after it has failed.

Primary and backup GPS are separate simulated devices with independent noise streams. A persistent primary sensor warning selects the backup and isolates the primary without clearing the primary fault. Three usable backup samples complete that recovery. There is no automatic return to primary.

Named chaos scenarios inject sensor and deadline conditions. They do not write faults or request modes. `docs/RECOVERY.md` is the recovery contract. `docs/ARCHITECTURE.md`, `docs/FAULT_MODEL.md`, and `docs/CHAOS_ENGINE.md` cover the rest.

## Build

The supported local toolchain is MSYS2 UCRT64. Put its binaries on `PATH`, then configure with Ninja:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
.\build\debug\ares.exe --duration-ms 250
```

`--duration-ms` defaults to 250. `--scenario` selects a named schedule (`nominal` by default). `--seed` sets the mission noise seed. `--record FILE` writes a mission recording. `--help` prints usage.

```powershell
.\build\debug\ares.exe --scenario gps_stale --seed 42 --record mission.bin
.\build\debug\ares-replay.exe mission.bin
```

Debug sanitizers (ASan and UBSan, or MSVC ASan) are a separate preset. GitHub Actions runs that preset with Clang on Ubuntu. MSYS2 UCRT64 GCC does not ship `libasan` or `libubsan`, so the preset stops at configure time on that toolchain.

```powershell
cmake --preset debug-sanitizers
cmake --build --preset debug-sanitizers
ctest --preset debug-sanitizers
```

Configuration downloads pinned GoogleTest 1.15.2.

## Layout

`src/core` is infrastructure: clock, log, events, periodic tasks, and thread lifetime. `src/flight` is the mode machine, fault handling, recovery, and the three tasks. `src/simulation` is the spacecraft, sensors, and chaos engine. `src/recorder` is the binary format and `ares-replay`. The executable composes them. Flight code does not depend on the simulator: GPS selection uses the `IGps` interface.

## Checks

```powershell
clang-format --dry-run --Werror (Get-ChildItem -Recurse include,src,tests -Include *.hpp,*.cpp)
clang-tidy -p build/debug --warnings-as-errors=* (Get-ChildItem src -Recurse -Filter *.cpp)
```

GitHub Actions runs the Debug sanitizer build, tests, `clang-format`, and `clang-tidy` on Ubuntu.
